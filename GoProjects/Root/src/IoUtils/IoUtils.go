package IoUtils

import (
	"errors"
	"io"
)

var (
	ErrAllDataWritten = errors.New("All data was written by LimitedWriter")
)

// "Хранилище" массивов байт
type ByteArrayStorage struct {
	// Канал для хранения массивов
	keeper chan []byte

	// Размер каждого из массивов
	array_capacity int
}

// Возвращает ёмкость каждого из хранимых массивов
func (s *ByteArrayStorage) Capacity() int {
	return s.array_capacity
}

// Добавление массива в хранилище
func (s *ByteArrayStorage) Push(buf []byte) {
	if cap(buf) < s.array_capacity {
		// Добавляемый буфер не подходит по размеру
		return
	}

	select {
	case s.keeper <- buf[:0]:
		// Добавили массив в хранилище
		break

	default:
		// Хранилище заполнено - удаляем массив
		break
	}
}

// Извлечение массива из хранилища
func (s *ByteArrayStorage) Pop() []byte {
	select {
	case buf := <-s.keeper:
		// Извлекли массив из хранилища
		return buf

	default:
		// Хранилище пусто - создаём новый массив
		return make([]byte, 0, s.array_capacity)
	}
}

func CreateByteArrayStorage(array_capacity int, storage_size uint16) ByteArrayStorage {
	return ByteArrayStorage{keeper: make(chan []byte, storage_size), array_capacity: array_capacity}
}

// Обёртка над каналом передачи буферов
type byte_chan_wrap struct {
	// Канал
	channel chan []byte

	// Непрочитанные данные
	data_to_read []byte

	buf_to_return []byte

	// Хранилище буферов
	buf_storage ByteArrayStorage
}

func (c *byte_chan_wrap) Read(buf []byte) (sz int, e error) {
	if len(buf) == 0 {
		// Некуда записывать считанные данные
		return 0, io.ErrShortBuffer
	}

	for run := true; run; run = run && len(buf) > 0 {
		if len(c.data_to_read) == 0 {
			if c.buf_to_return != nil {
				// Возвращаем буфер в хранилище
				c.buf_storage.Push(c.buf_to_return)
				c.buf_to_return = nil
			}
			c.data_to_read = nil

			// Считываем данные из канала
			select {
			case data, success := <-c.channel:
				if success {
					// Данные из канала получены
					c.data_to_read = data
					c.buf_to_return = data
				} else {
					// Канал закрыт
					e = io.EOF
					run = false
					continue
				}
			default:
				// Данных в канале нет
				run = false
				continue
			}
		} // if len(c.data_to_read) == 0

		// Записываем данные в буфер
		sz_to_copy := len(c.data_to_read)
		buf_sz := len(buf)
		if sz_to_copy > buf_sz {
			sz_to_copy = buf_sz
		}

		for t := 0; t < sz_to_copy; t++ {
			buf[t] = c.data_to_read[t]
		}

		buf = buf[sz_to_copy:]
		c.data_to_read = c.data_to_read[sz_to_copy:]
		sz += sz_to_copy
	} // for run := true; run; run = run && len(buf) > 0

	return
}

func (c *byte_chan_wrap) Write(buf []byte) (sz int, e error) {
	defer func() {
		if p := recover(); p != nil {
			// Почти наверняка канал уже закрыт
			err, check := p.(error)
			if check {
				// В панику передали ошибку
				e = err
			} else {
				e = io.EOF
			}
		}
	}()

	for l := len(buf); l > 0; l = len(buf) {
		send_buf := c.buf_storage.Pop()
		copy_sz := cap(send_buf)
		if copy_sz > l {
			copy_sz = l
		}

		c.channel <- append(send_buf, buf[:copy_sz]...)
		buf = buf[copy_sz:]
		sz += copy_sz
	}
	return
}

func (c *byte_chan_wrap) Close() (e error) {
	defer recover()
	close(c.channel)
	return
}

// Создаёт канал, в котором io.Writer.Write не ждёт, когда закончится операция считывания
// Оба конца канала (читатель и писатель) потоконебезопасные
func NonBlockingPipe(one_piece_max_sz int, chan_sz uint16) (io.ReadCloser, io.WriteCloser) {
	c := &byte_chan_wrap{channel: make(chan []byte, chan_sz), buf_storage: CreateByteArrayStorage(one_piece_max_sz, 2*chan_sz)}
	return c, c
}

func limit_write(w io.Writer, buf []byte, left_data_sz *int64) (int, error) {
	left_to_write := *left_data_sz
	if left_to_write <= 0 {
		return 0, ErrAllDataWritten
	}

	if int64(len(buf)) > left_to_write {
		buf = buf[:left_to_write]
	}

	sz, e := w.Write(buf)
	if sz > 0 {
		left_to_write -= int64(sz)

		if (left_to_write == 0) && (e == nil) {
			e = ErrAllDataWritten
		}
	}

	*left_data_sz = left_to_write
	return sz, e
} // func limit_write(w io.Writer, []byte buf, left_data *int64) (int, error)

type LimitedWriter struct {
	writer        io.Writer
	left_to_write int64
}

func (w *LimitedWriter) Write(buf []byte) (int, error) {
	return limit_write(w.writer, buf, &w.left_to_write)
} // func (w *LimitedWriter) Write(buf []byte) (int, error)

func LimitWriter(w io.Writer, data_sz int64) io.Writer {
	return &LimitedWriter{writer: w, left_to_write: data_sz}
}

type LimitedWriteCloser struct {
	writer        io.WriteCloser
	left_to_write int64
}

func (w *LimitedWriteCloser) Write(buf []byte) (int, error) {
	return limit_write(w.writer, buf, &w.left_to_write)
} // func (w *LimitedWriteCloser) Write(buf []byte) (int, error)

func (w *LimitedWriteCloser) Close() error {
	return w.writer.Close()
}

func LimitWriteCloser(w io.WriteCloser, data_sz int64) io.WriteCloser {
	return &LimitedWriteCloser{writer: w, left_to_write: data_sz}
}

// Интерфейс "Преобразователь"
type Converter interface {
	// Преобразование данных в заданном буфере.
	Convert([]byte)
}

// Структура-"сериализатор" пакетов
type serializer struct {
	// Заголовок
	header []byte

	// Данные пакета
	data io.Reader

	data_worker Converter

	// Окончание пакета
	pack_end []byte
}

func (s *serializer) Read(buf []byte) (sz int, err error) {
	buf_len := len(buf)
	head_len := len(s.header)
	if head_len > 0 {
		// Не весь заголовок прочитан
		if head_len > buf_len {
			head_len = buf_len
		}

		for t, v := range s.header[:head_len] {
			buf[t] = v
		}

		sz += head_len
		buf = buf[head_len:]
		s.header = s.header[head_len:]
		if len(s.header) == 0 {
			s.header = nil
		}

		if len(buf) == 0 {
			return
		}
	} // if head_len > 0

	// Считывание данных пакета
	var converter func([]byte)
	if s.data_worker != nil {
		converter = func(b []byte) {
			s.data_worker.Convert(b)
		}
	} else {
		converter = func([]byte) {}
	}

	for (len(buf) > 0) && (s.data != nil) {
		s_, e := s.data.Read(buf)
		if s_ > 0 {
			sz += s_
			converter(buf[:s_])
			buf = buf[s_:]
		}

		if e != nil {
			// Ошибка чтения - считаем, что все данные прочитаны
			s.data = nil
		}
	} // for (len(buf) > 0) && (s.data != nil)

	buf_len = len(buf)
	if buf_len == 0 {
		return
	}

	end_len := len(s.pack_end)
	if end_len > buf_len {
		end_len = buf_len
	}

	for t, v := range s.pack_end[:end_len] {
		buf[t] = v
	}

	sz += end_len
	s.pack_end = s.pack_end[end_len:]
	if len(s.pack_end) == 0 {
		// Весь пакет прочитан
		s.pack_end = nil
		err = io.EOF
	}

	return
} // func (s *serializer) Read(buf []byte) (int, error )

func Serialize(pack_header []byte, pack_data io.Reader, pack_data_converter Converter, pack_end []byte) io.Reader {
	return &serializer{header: pack_header, data: pack_data, data_worker: pack_data_converter, pack_end: pack_end}
}

type CommonConverter struct {
	ConvertFunc func([]byte)
}

func (c CommonConverter) Convert(buf []byte) {
	if c.ConvertFunc != nil {
		c.ConvertFunc(buf)
	}
}
