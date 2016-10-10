package CommonTcpServer

import (
	"bytes"
	"io"
	"net"
	"sync"
	"testing"
	"time"
)

func TestCommon(t *testing.T) {
	cwc1 := common_write_closer{}

	buf := make([]byte, 5)
	for t := 0; t < len(buf); t++ {
		buf[t] = byte(t)
	}

	sz, e := cwc1.Write(buf)
	if (sz != 0) || (e != io.EOF) {
		t.Error("Write должен возвращать 0 и ", io.EOF, ", вместо этого - ", sz, " и ", e)
	}

	e = cwc1.Close()
	if e != nil {
		t.Error("Close вернул ", e, " вместо nil")
	}

	cwc2 := common_write_closer{h_writer: func(b []byte) (int, error) { return len(b), nil },
		h_closer: func() error { return io.EOF }}

	sz, e = cwc2.Write(buf)
	if (sz != len(buf)) || (e != nil) {
		t.Error("Write должен возвращать ", len(buf), " и nil, вместо этого - ", sz, " и ", e)
	}

	e = cwc2.Close()
	if e != io.EOF {
		t.Error("Close вернул ", e, " вместо ", io.EOF)
	}
}

type write_closer_wrap struct {
	wc io.WriteCloser
}

func (w *write_closer_wrap) Write(p []byte) (n int, err error) {
	return w.wc.Write(p)
}

func (w *write_closer_wrap) Close() error {
	return w.wc.Close()
}

func (w *write_closer_wrap) CloseWithError(e error) error {
	return w.wc.Close()
}

func TestServer(t *testing.T) {
	srv, err := RunNewServer("127.0.0.1", 45000, func(wc io.WriteCloser) DataConsumer { return &write_closer_wrap{wc: wc} }, 10, 10, nil)
	if (srv == nil) || (err != nil) {
		if err == nil {
			t.Fatal("Ошибка запуска сервера: RunNewServer вернул nil, nil")
		} else {
			t.Fatal("Ошибка запуска сервера: ", err)
		}

	}
	defer srv.Close()

	srv2, err2 := RunNewServer("127.0.0.1", 45000, func(wc io.WriteCloser) DataConsumer { return &write_closer_wrap{wc: wc} }, 10, 10, nil)
	if (srv2 != nil) || (err2 == nil) {
		t.Error("Ошибка: запущено более одного сервера на одном и том же адресе")
	}

	if e := srv.Close(); e != nil {
		t.Fatalf("Ошибка остановки сервера: %s\n", e.Error())
	}
	srv, err = RunNewServer("127.0.0.1", 45000, func(wc io.WriteCloser) DataConsumer { return &write_closer_wrap{wc: wc} }, 10, 10, nil)
	if (srv == nil) || (err != nil) {
		if err == nil {
			t.Fatal("Ошибка повторного запуска сервера: RunNewServer вернул nil, nil")
		} else {
			t.Fatal("Ошибка повторного запуска сервера: ", err)
		}

	}

	var counter sync.WaitGroup

	test_func := func() {
		defer counter.Done()

		c, e := net.Dial("tcp", "127.0.0.1:45000")
		if e != nil {
			t.Fatal("Ошибка подключения к серверу: ", e)
		}

		defer c.Close()
		buf1 := []byte{1, 2, 3}
		buf2 := make([]byte, len(buf1))

		for i := 0; i < 100; i++ {
			// t.Log("Попытка ", i+1)
			sz, e := c.Write(buf1)
			if (sz != len(buf1)) || (e != nil) {
				t.Error("Ошибка отправки данных: ", sz, "; ", e)
			} else {
				sz, e = c.Read(buf2)
				if (sz != len(buf1)) || (e != nil) {
					t.Error("Ошибка считывания данных данных: ", sz, "; ", e)
				} else if bytes.Compare(buf1, buf2) != 0 {
					t.Error("Считанные (", buf2, ") и полученные (", buf2, ") данные отличаются")
				}
			}

			for t := 0; t < len(buf1); t++ {
				buf1[t] = buf1[t] + byte(t)
			}
		} // for i := 0; i < 100; i++ {
	} // test_func := func() {

	for n := 0; n < 20; n++ {
		counter.Add(1)
		go test_func()
	}

	counter.Wait()

	test_func2 := func() {
		defer counter.Done()

		c, e := net.Dial("tcp", "127.0.0.1:45000")
		if e != nil {
			t.Fatal("Ошибка подключения к серверу: ", e)
		}
		defer c.Close()

		buf1 := []byte{1, 2, 3}
		buf2 := make([]byte, len(buf1))

		for {
			// t.Log("Попытка ", i+1)
			sz, e := c.Write(buf1)
			if (sz == len(buf1)) && (e == nil) {
				sz, e = c.Read(buf2)
				if (sz == len(buf1)) && (e == nil) && bytes.Compare(buf1, buf2) != 0 {
					t.Error("Считанные (", buf2, ") и полученные (", buf2, ") данные отличаются")
				}
			}

			if e != nil {
				break
			}

			for t := 0; t < len(buf1); t++ {
				buf1[t] = buf1[t] + byte(t)
			}
		} // for {
	} // test_func2 := func() {

	for n := 0; n < 20; n++ {
		counter.Add(1)
		go test_func2()
	}

	time.Sleep(100 * time.Millisecond)

	if e := srv.Close(); e != nil {
		t.Fatalf("Ошибка остановки сервера: %s\n", e.Error())
	}
	counter.Wait()
} // func TestServer(t *testing.T)

func TestServerDeep(t *testing.T) {
	srv, err := RunNewServer("127.0.0.1", 45000, func(wc io.WriteCloser) DataConsumer { return &write_closer_wrap{wc: wc} }, 100, 10000, nil)
	if (srv == nil) || (err != nil) {
		if err == nil {
			t.Fatal("Ошибка запуска сервера: RunNewServer вернул nil, nil")
		} else {
			t.Fatal("Ошибка запуска сервера: ", err)
		}

	}
	defer srv.Close()

	var counter sync.WaitGroup

	test_func := func() {
		defer counter.Done()

		c, e := net.Dial("tcp", "127.0.0.1:45000")
		if e != nil {
			t.Error("Ошибка подключения к серверу: ", e)
			return
		}

		defer c.Close()
		buf1 := []byte{1, 2, 3}
		buf2 := make([]byte, len(buf1))

		for i := 0; i < 5; i++ {
			sz, e := c.Write(buf1)
			if (sz != len(buf1)) || (e != nil) {
				t.Error("Ошибка отправки данных: ", sz, "; ", e)
				return
			} else {
				sz, e = c.Read(buf2)
				if (sz != len(buf1)) || (e != nil) {
					t.Error("Ошибка считывания данных данных: ", sz, "; ", e)
					return
				} else if bytes.Compare(buf1, buf2) != 0 {
					t.Error("Считанные (", buf2, ") и полученные (", buf2, ") данные отличаются")
					return
				}
			}

			for t := 0; t < len(buf1); t++ {
				buf1[t] = buf1[t] + byte(t)
			}
		} // for i := 0; i < 100; i++ {
	} // test_func := func() {

	for step := 0; step < 10; step++ {
		for n := 0; n < 1000; n++ {
			counter.Add(1)
			go test_func()
		}
		counter.Wait()
	}

	if e := srv.Close(); e != nil {
		t.Fatalf("Ошибка остановки сервера: %s\n", e.Error())
	}
} // func TestServerDeep(t *testing.T)
