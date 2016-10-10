package IoUtils

import (
	"bytes"
	"io"
	"sync"
	"testing"
)

func TestByteArrayStorage(t *testing.T) {
	var array_capacity int = 1000
	var storage_size uint16 = 10
	storage := CreateByteArrayStorage(array_capacity, storage_size)

	wg := sync.WaitGroup{}
	for i := 0; i < 10; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()

			for j := 0; j < 100; j++ {
				buf := storage.Pop()
				if len(buf) > 0 {
					t.Error("Хранилище вернуло непустой массив")
				} else if cap(buf) != array_capacity {
					t.Error("Хранилище вернуло буфер с ёмкостью, отличной от заданной")
				}

				storage.Push(buf)
			}
		}()
	}
	wg.Wait()
} // func TestByteArrayStorage(t *testing.T)

func TestPipe(t *testing.T) {
	checker := func(close_writer bool) {
		r, w := NonBlockingPipe(100, 1)
		if (r == nil) || (w == nil) {
			t.Fatal("Один или оба конца канала нулевые")
		}

		buf := []byte("1234567890")
		sz, e := w.Write(buf)
		if (sz != len(buf)) || (e != nil) {
			t.Fatal("Ошибка записи")
		}

		sz, e = r.Read(buf)
		if (sz != len(buf)) || (e != nil) || (string(buf) != "1234567890") {
			t.Fatal("Ошибка чтения")
		}

		if close_writer {
			if w.Close() != nil {
				t.Fatal("Ошибка закрытия писателя")
			}
		} else {
			if r.Close() != nil {
				t.Fatal("Ошибка закрытия читателя")
			}
		}

		sz, e = r.Read(buf)
		if (sz != 0) || (e == nil) {
			t.Error("Чтение из закрытого канала не вернуло ошибку")
		}

		sz, e = w.Write(buf)
		if (sz != 0) || (e == nil) {
			t.Error("Запись в закрытый канал не вернула ошибку")
		}
	}

	checker(true)
	checker(false)

	r, w := NonBlockingPipe(1000, 5)

	data1 := "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890"
	var data2 string

	var counter sync.WaitGroup
	counter.Add(1)
	go func() {
		defer counter.Done()
		buf := make([]byte, 7)
		for sz, _ := r.Read(buf); sz > 0; sz, _ = r.Read(buf) {
			data2 += string(buf[:sz])
		}
	}()

	to_send := []byte(data1)
	for len(to_send) > 0 {
		l := 15
		if l > len(to_send) {
			l = len(to_send)
		}
		if sz, e := w.Write(to_send[:l]); (sz != l) || (e != nil) {
			t.Fatal("Ошибка отправки (в цикле)")
		}

		to_send = to_send[l:]
	}

	counter.Wait()
	if data1 != data2 {
		t.Errorf("Ошибка проверки чтения-записи: ожидали %s, получили %s\n", data1, data2)
	}
} // func TestPipe(t *testing.T)

type simple_write_closer struct {
	ch chan []byte
}

func (w *simple_write_closer) Write(buf []byte) (sz int, e error) {
	defer func() {
		if recover() != nil {
			sz, e = 0, io.EOF
		}
	}()

	sz = len(buf)
	w.ch <- append([]byte{}, buf...)
	return
}

func (w *simple_write_closer) Close() error {
	close(w.ch)
	return nil
}

func TestLimitWrite(t *testing.T) {
	{
		ch := make(chan []byte, 10)
		write_closer := simple_write_closer{ch}

		lw := LimitWriter(&write_closer, 10)
		sz, e := lw.Write([]byte("qaz"))
		if (sz != 3) || (e != nil) {
			t.Fatalf("Ошибка записи: sz = %d; e = %s\n", sz, e.Error())
		}

		select {
		case data := <-ch:
			if string(data) != "qaz" {
				t.Fatal("Ошибка чтения записанных данных")
			}

		default:
			t.Fatal("Данные не были записаны")
		}

		sz, e = lw.Write([]byte("qazqwerty"))
		if (sz != 7) || (e != ErrAllDataWritten) {
			t.Fatalf("Ошибка записи: sz = %d; e = %s\n", sz, e.Error())
		}

		select {
		case data := <-ch:
			if string(data) != "qazqwer" {
				t.Fatal("Ошибка чтения записанных данных")
			}

		default:
			t.Fatal("Данные не были записаны")
		}
	}

	{
		ch := make(chan []byte, 10)
		write_closer := simple_write_closer{ch}

		lw := LimitWriteCloser(&write_closer, 10)
		sz, e := lw.Write([]byte("qaz"))
		if (sz != 3) || (e != nil) {
			t.Fatalf("Ошибка записи: sz = %d; e = %s\n", sz, e.Error())
		}

		select {
		case data := <-ch:
			if string(data) != "qaz" {
				t.Fatal("Ошибка чтения записанных данных")
			}

		default:
			t.Fatal("Данные не были записаны")
		}

		e = lw.Close()
		if e != nil {
			t.Fatalf("Ошибкаа закрытия: %s\n", e.Error())
		}

		sz, e = lw.Write([]byte("qazqwerty"))
		if (sz > 0) || (e != io.EOF) {
			t.Fatalf("Ошибка записи после закрытия: sz = %d; e = %s\n", sz, e.Error())
		}

		select {
		case data, success := <-ch:
			if success {
				t.Fatalf("Пришли данные после закрытия: %s\n", string(data))
			}

		default:
			t.Fatal("Канал не был закрыт после вызова close")
			break
		}
	}
} // func TestLimitWrite(t *testing.T)

type test_converter struct {
}

func (test_converter) Convert(buf []byte) {
	for t := range buf {
		buf[t]++
	}
}

func TestSerializer(t *testing.T) {
	// Проверяем serializer
	conv := &test_converter{}
	var ser io.Reader = &serializer{header: []byte("qazqwerty"),
		data:        bytes.NewReader([]byte("HelloПривет")),
		data_worker: conv,
		pack_end:    []byte("123")}
	var serialized_data []byte
	buf := make([]byte, 3)
	for sz, _ := ser.Read(buf); sz > 0; sz, _ = ser.Read(buf) {
		serialized_data = append(serialized_data, buf[:sz]...)
	}

	serialized_str := string(serialized_data)
	expected := []byte("qazqwertyHelloПривет123")
	conv.Convert(expected[9:(len(expected) - 3)])
	expected_str := string(expected)

	if serialized_str != expected_str {
		t.Fatalf("Ошибка в serializer-е: ожидали %s, получили %s\n", expected_str, serialized_str)
	}

	ser = Serialize([]byte("qazqwerty"), bytes.NewReader([]byte("HelloПривет")), conv, []byte("123"))
	serialized_data = serialized_data[:0]
	for sz, _ := ser.Read(buf); sz > 0; sz, _ = ser.Read(buf) {
		serialized_data = append(serialized_data, buf[:sz]...)
	}

	serialized_str = string(serialized_data)
	if serialized_str != expected_str {
		t.Fatalf("Ошибка в serializer-е: ожидали %s, получили %s\n", expected_str, serialized_str)
	}
} // func TestSerializer(t *testing.T)
