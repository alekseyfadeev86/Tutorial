package WebsocketUtils

/*
Утилиты поддержки протокола web-сокетов
*/

import (
	"crypto/sha1"
	"encoding/base64"
	"errors"
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

func (f *Frame) Serialize() (res []byte) {
	if f.Type > 0xF {
		panic(FrameOpcodeError)
	}

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

	var tmp_sl []byte
	if frame_data_len < 126 {
		v |= byte(frame_data_len)
	} else if frame_data_len <= 0xFFFF {
		v |= 126
		tmp_sl = []byte{byte(0xFF & (frame_data_len >> 8)), byte(0xFF & frame_data_len)}
	} else {
		v |= 127
		tmp_sl = make([]byte, 0, 8)
		for i := int8(7); i > -1; i-- {
			tmp_sl = append(tmp_sl, byte(0xFF&(frame_data_len>>(8*byte(i)))))
		}
	}

	res = append(res, v)
	res = append(res, tmp_sl...)
	tmp_sl = tmp_sl[0:0]

	if f.Mask {
		res = append(res, f.MaskKey[0:]...)
		for i, v := range f.Data {
			res = append(res, f.MaskKey[i%4]^v)
		}
	} else {
		res = append(res, f.Data...)
	}

	return
}

type FrameReader struct {
	// Необработанные данные
	unworked_data []byte

	// Недоформированный пакет
	unfinished_frame *Frame

	// Размер необходимых данных для формирования данных пакета
	left_data_sz uint64
}

func (r *FrameReader) OnRead(data []byte) {
	r.unworked_data = append(r.unworked_data, data...)
}

func (r *FrameReader) ParseOne() (res *Frame) {
	if r.unfinished_frame == nil {
		head_len := 2 // Длина заголовка (минимум 2 байта)
		data_len := len(r.unworked_data)
		if data_len < head_len {
			// Недостаточно данных даже для формироваиня заголовка
			return
		}

		if (0x80 & r.unworked_data[1]) != 0 {
			// В заголовке присутствует маска - +4 байта к заголовку
			head_len += 4
		}
		ln_param := 0x7F & r.unworked_data[1]
		if ln_param == 126 {
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
			for _, v := range r.unworked_data[3:5] {
				r.left_data_sz <<= 8
				r.left_data_sz |= uint64(v)
			}
		} else if r.left_data_sz > 126 {
			r.left_data_sz = 0
			for _, v := range r.unworked_data[3:11] {
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
		res = r.unfinished_frame
		r.unfinished_frame = nil
	}

	return
} // func (r *FrameReader) ParseOne() (res *Frame)
