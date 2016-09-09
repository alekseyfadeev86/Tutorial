package WebsocketUtils

/*
Утилиты поддержки протокола web-сокетов
*/

import (
	"IoUtils"
	"bytes"
	"crypto/sha1"
	"encoding/base64"
	"errors"
	"io"
)

const (
	postfix = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

	// текстовый фрейм.
	OpcodeText byte = 0x1

	// двоичный фрейм
	OpcodeBin byte = 0x2

	// фрейм закрытия соединения
	OpcodeClose byte = 0x8

	// пинг
	OpcodePing byte = 0x9

	// ответ на пинг
	OpcodePong byte = 0xA

	// фрейм-продолжение для фрагментированного сообщения и
	// интерпретируется, исходя из ближайшего предыдущего ненулевого типа
	OpcodeContinue byte = 0x0
)

var (
	FrameOpcodeError = errors.New("Incorrect frame opcode (must be less than 16)!")
)

// Возвращает ключ подтверждения, соответствующий ключу key
func GetAcceptKey(key string) string {
	sha_sum := sha1.Sum([]byte(key + postfix))
	return base64.StdEncoding.EncodeToString(sha_sum[0:])
}

// Структура пакета
type Frame struct {
	// Признак последнего пакета
	Fin bool

	// Для будущего применения (Rsvs[i] соответствует rsvi+1)
	Rsvs [3]bool

	// Тип (opcode)
	Type byte

	// Признак маски
	Mask bool

	// Ключ маски
	MaskKey [4]byte

	// Данные
	Data []byte
}

// Формирование набора байт для отправки
func (f *Frame) Serialize() (res []byte) {
	if f.Type > 0xF {
		// Тип (опкод) не может быть больше 15
		panic(FrameOpcodeError)
	}

	// Размер данных
	frame_data_len := len(f.Data)
	res = make([]byte, 0, 16+frame_data_len)

	var v byte = f.Type
	if f.Fin {
		v |= 0x80
	}

	for t := byte(0); t < 3; t++ {
		if f.Rsvs[t] {
			v |= byte(0x40) >> t
		}
	}

	res = append(res, v)

	if f.Mask {
		v = 0x80
	} else {
		v = 0
	}

	if frame_data_len < 126 {
		v |= byte(frame_data_len)
		res = append(res, v)
	} else if frame_data_len <= 0xFFFF {
		v |= 126
		res = append(res, v)
		res = append(res, byte(0xFF&(frame_data_len>>8)))
		res = append(res, byte(0xFF&frame_data_len))
	} else {
		v |= 127
		res = append(res, v)
		for i := int8(7); i > -1; i-- {
			res = append(res, byte(0xFF&(frame_data_len>>(8*byte(i)))))
		}
	}

	if f.Mask {
		res = append(res, f.MaskKey[0:]...)
		for i, v := range f.Data {
			res = append(res, f.MaskKey[i%4]^v)
		}
	} else {
		res = append(res, f.Data...)
	}

	return
} // func (f *Frame) Serialize() (res []byte)

// Структура разбора пакетов, полученных в бинарном виде
type FrameParser struct {
	// Необработанные данные
	unworked_data []byte

	// Недоформированный пакет
	unfinished_frame *Frame

	// Размер необходимых данных для формирования данных пакета
	left_data_sz uint64
}

// Запись данных для обработки в структуру
func (r *FrameParser) Write(data []byte) (int, error) {
	r.unworked_data = append(r.unworked_data, data...)
	return len(data), nil
}

// Разбор полученных данных
func (r *FrameParser) Parse() (res []Frame) {
	for len(r.unworked_data) > 0 {
		if r.unfinished_frame == nil {
			// Начинаем формировать новый пакет
			head_len := 2 // Длина заголовка (минимум 2 байта)
			data_len := len(r.unworked_data)

			if data_len < head_len {
				// Недостаточно данных даже для формирования заголовка
				return
			}

			if (0x80 & r.unworked_data[1]) != 0 {
				// В заголовке присутствует маска - +4 байта к заголовку
				head_len += 4
			}

			if ln_param := 0x7F & r.unworked_data[1]; ln_param == 126 {
				// Поле длины пакета занимает лишние 2 байта
				head_len += 2
			} else if ln_param == 127 {
				// Поле длины пакета занимает лишние 8 байт
				head_len += 8
			}

			if data_len < head_len {
				// Недостаточно данных даже для формироваиня заголовка
				return
			}

			// Формируем заголовок пакета
			new_frame := &Frame{}
			new_frame.Fin = (0x80 & r.unworked_data[0]) != 0
			for t := byte(0); t < 3; t++ {
				new_frame.Rsvs[t] = (r.unworked_data[0] & (byte(0x40) >> t)) != 0
			}

			new_frame.Type = 0xF & r.unworked_data[0]
			new_frame.Mask = (0x80 & r.unworked_data[1]) != 0
			if new_frame.Mask {
				for i := 0; i < 4; i++ {
					new_frame.MaskKey[i] = r.unworked_data[head_len-4+i]
				}
			}

			r.left_data_sz = uint64(0x7F & r.unworked_data[1])
			if r.left_data_sz == 126 {
				r.left_data_sz = 0
				for _, v := range r.unworked_data[2:4] {
					r.left_data_sz <<= 8
					r.left_data_sz |= uint64(v)
				}
			} else if r.left_data_sz > 126 {
				r.left_data_sz = 0
				for _, v := range r.unworked_data[2:10] {
					r.left_data_sz <<= 8
					r.left_data_sz |= uint64(v)
				}
			}

			r.unfinished_frame = new_frame
			r.unworked_data = r.unworked_data[head_len:]
			if len(r.unworked_data) == 0 {
				r.unworked_data = nil
			}
		} // if r.unfinished_frame == nil

		// Формируем данные пакета
		cut_len := uint64(len(r.unworked_data))
		if cut_len > r.left_data_sz {
			cut_len = r.left_data_sz
		}

		r.unfinished_frame.Data = append(r.unfinished_frame.Data, r.unworked_data[:cut_len]...)
		r.unworked_data = r.unworked_data[cut_len:]
		if len(r.unworked_data) == 0 {
			r.unworked_data = nil
		}

		r.left_data_sz -= cut_len
		if r.left_data_sz == 0 {
			// Расшифровываем данные, если указана маска
			if r.unfinished_frame.Mask {
				for i := range r.unfinished_frame.Data {
					r.unfinished_frame.Data[i] = r.unfinished_frame.Data[i] ^ r.unfinished_frame.MaskKey[i%4]
				}
			}

			// Пакет сформирован
			res = append(res, *r.unfinished_frame)
			r.unfinished_frame = nil
		}
	} // for len(r.unworked_data) > 0

	return
} // func (r *FrameParser) Parse() (res []Frame)

// Структура большого пакета
type BigFrame struct {
	// Признак последнего пакета
	Fin bool

	// Для будущего применения (Rsvs[i] соответствует rsvi+1)
	Rsvs [3]bool

	// Тип (opcode)
	Type byte

	// Признак маски
	Mask bool

	// Ключ маски
	MaskKey [4]byte

	// Данные
	Data io.Reader
}

// Структура накладывания маски
type frame_data_converter struct {
	mask_key [4]byte

	shift uint8
}

func (c *frame_data_converter) Convert(data []byte) {
	for i := range data {
		c.shift = c.shift % 4
		data[i] = data[i] ^ c.mask_key[c.shift]
		c.shift++
	}
}

// Формирование набора байт для отправки
func Serialize(fin bool, rsvs [3]bool, frame_type byte, mask bool, mask_key [4]byte, data_len uint64, data io.Reader) io.Reader {
	if frame_type > 0xF {
		// Тип (опкод) не может быть больше 15
		panic(FrameOpcodeError)
	}

	// Размер данных
	header := make([]byte, 0, 16)

	var v byte = frame_type
	if fin {
		v |= 0x80
	}

	for t := byte(0); t < 3; t++ {
		if rsvs[t] {
			v |= byte(0x40) >> t
		}
	}

	header = append(header, v)

	if mask {
		v = 0x80
	} else {
		v = 0
	}

	if data_len < 126 {
		v |= byte(data_len)
		header = append(header, v)
	} else if data_len <= 0xFFFF {
		v |= 126
		header = append(header, v)
		header = append(header, byte(0xFF&(data_len>>8)))
		header = append(header, byte(0xFF&data_len))
	} else {
		v |= 127
		header = append(header, v)
		for i := int8(7); i > -1; i-- {
			header = append(header, byte(0xFF&(data_len>>(8*byte(i)))))
		}
	}

	var conv IoUtils.Converter
	if mask {
		header = append(header, mask_key[:]...)
		conv = &frame_data_converter{mask_key: mask_key}
	}

	return IoUtils.Serialize(header, data, conv, nil)
} // func (f *Frame) Serialize() (res []byte)

// Структура разбора пакетов, полученных в бинарном виде
type BigFrameParser struct {
	// Необработанные данные
	unworked_data []byte

	// "Писатель" для данных пакета
	data_writer io.WriteCloser
}

// Запись данных для обработки в структуру
func (p *BigFrameParser) Write(data []byte) (int, error) {
	res := len(data)

	for (len(data) > 0) && (p.data_writer != nil) {
		s, e := p.data_writer.Write(data)
		if s > 0 {
			// Данные записаны (все или часть)
			if (s < len(data)) && (e == nil) {
				// Были записаны не все данные, но ошибка нулевая
				panic(errors.New("(s < len(data)) && (e == nil)"))
			}

			data = data[s:]
		}

		if e != nil {
			// Ошибка записи - считаем, что все данные пакета записаны
			p.data_writer.Close()
			p.data_writer = nil
		}
	}

	p.unworked_data = append(p.unworked_data, data...)
	return res, nil
} // func (p *BigFrameParser) Write(data []byte) (int, error)

func (parser *BigFrameParser) Close() (e error) {
	if parser.data_writer != nil {
		e = parser.data_writer.Close()
		parser.data_writer = nil
	}

	return
}

// Разбор полученных данных
func (p *BigFrameParser) Parse() (res []BigFrame) {
	for len(p.unworked_data) > 0 {
		if p.data_writer != nil {
			panic(errors.New("Не закончили запись данных предыдущего пакета, а уже есть данные для нового"))
		}

		// Начинаем формировать новый пакет
		head_len := 2 // Длина заголовка (минимум 2 байта)
		data_len := len(p.unworked_data)

		if data_len < head_len {
			// Недостаточно данных даже для формирования заголовка
			return
		}

		if (0x80 & p.unworked_data[1]) != 0 {
			// В заголовке присутствует маска - +4 байта к заголовку
			head_len += 4
		}

		if ln_param := 0x7F & p.unworked_data[1]; ln_param == 126 {
			// Поле длины пакета занимает лишние 2 байта
			head_len += 2
		} else if ln_param == 127 {
			// Поле длины пакета занимает лишние 8 байт
			head_len += 8
		}

		if data_len < head_len {
			// Недостаточно данных даже для формироваиня заголовка
			return
		}

		// Формируем заголовок пакета
		new_frame := BigFrame{}
		new_frame.Fin = (0x80 & p.unworked_data[0]) != 0
		for t := byte(0); t < 3; t++ {
			new_frame.Rsvs[t] = (p.unworked_data[0] & (byte(0x40) >> t)) != 0
		}

		new_frame.Type = 0xF & p.unworked_data[0]
		new_frame.Mask = (0x80 & p.unworked_data[1]) != 0
		if new_frame.Mask {
			for i := 0; i < 4; i++ {
				new_frame.MaskKey[i] = p.unworked_data[head_len-4+i]
			}
		}

		data_sz := uint64(0x7F & p.unworked_data[1])
		if data_sz == 126 {
			data_sz = 0
			for _, v := range p.unworked_data[2:4] {
				data_sz <<= 8
				data_sz |= uint64(v)
			}
		} else if data_sz > 126 {
			data_sz = 0
			for _, v := range p.unworked_data[2:10] {
				data_sz <<= 8
				data_sz |= uint64(v)
			}
		}

		p.unworked_data = p.unworked_data[head_len:]
		if len(p.unworked_data) == 0 {
			p.unworked_data = nil
		}

		// Формируем данные пакета
		if data_sz <= uint64(len(p.unworked_data)) {
			// Считано достаточно данных для форрмирования тела
			frame_data := p.unworked_data[:data_sz]
			p.unworked_data = p.unworked_data[data_sz:]
			if len(p.unworked_data) == 0 {
				p.unworked_data = nil
			}

			if new_frame.Mask {
				// Задана маска - расшифровываем данные
				for i := range frame_data {
					frame_data[i] = frame_data[i] ^ new_frame.MaskKey[i%4]
				}
			}

			new_frame.Data = bytes.NewReader(frame_data)
		} else {
			// Всё тело ещё не считали
			var one_piece_max_sz int = int(data_sz)
			var chan_sz uint8 = 2

			if one_piece_max_sz > 10240 {
				one_piece_max_sz = 10240
				chan_sz = 10
			}

			reader, writer := IoUtils.NonBlockingPipe(one_piece_max_sz, chan_sz)
			p.data_writer = IoUtils.LimitWriteCloser(writer, int64(data_sz))
			var conv IoUtils.Converter
			if new_frame.Mask {
				conv = &frame_data_converter{mask_key: new_frame.MaskKey}
			}
			new_frame.Data = IoUtils.Serialize(nil, io.LimitReader(reader, int64(data_sz)), conv, nil)

			if p.unworked_data != nil {
				sz, e := p.data_writer.Write(p.unworked_data)
				if (sz < len(p.unworked_data)) || (e != nil) {
					// Были записаны не все данные
					panic(errors.New("Ошибка записи тела запроса")) // Возможно, стоит убрать
				}

				p.unworked_data = nil
			}
		}

		// Пакет сформирован
		res = append(res, new_frame)
	} // for len(r.unworked_data) > 0

	return
} // func (p *BigFrameParser) Parse() (res []BigFrame)
