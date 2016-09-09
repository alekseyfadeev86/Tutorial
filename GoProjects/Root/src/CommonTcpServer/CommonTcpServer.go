package CommonTcpServer

import (
	"io"
	"net"
	"strconv"
	"sync"
	"sync/atomic"
)

// Интерфейс "потребитель данных" (от сервера или от пользователя)
type DataConsumer interface {
	// Содержит функции Write(p []byte) (n int, err error) и Close() error
	io.WriteCloser

	// Закрытие "потребителя данных" с указанием причины
	CloseWithError(error) error
}

// Обобщённая структура, поддерживающая интерфейс io.WriteCloser
type common_write_closer struct {
	// Функция отправки данных
	h_writer func([]byte) (int, error)

	// Функция закрытия
	h_closer func() error
}

func (cwc *common_write_closer) Write(buf []byte) (int, error) {
	if cwc.h_writer != nil {
		return cwc.h_writer(buf)
	}

	return 0, io.EOF
}

func (cwc *common_write_closer) Close() error {
	if cwc.h_closer != nil {
		return cwc.h_closer()
	}

	return nil
}

// Тип функции-создателя обработчиков данных
type DataWorkersMaker func(io.WriteCloser) DataConsumer

// Тип сервера
type Server struct {
	// Канал для отправки уведомления о завершении работы сервера
	stopper chan interface{}

	// Счётчик запущенных параллельных подпрограмм
	goroutines_counter sync.WaitGroup

	// Не равен нулю, если сервер в процессе остановки или уже остановлен
	must_be_stopped int32

	// Приёмник соединений
	listener net.Listener
}

// Запуск сервера
func (s *Server) start(host string, port uint16, dw_maker DataWorkersMaker, printer func(msg interface{})) error {
	port_str := strconv.Itoa(int(port))
	full_host := host + ":" + port_str

	var err error

	// Создаём приёмник соединений
	s.listener, err = net.Listen("tcp", full_host)
	if err != nil {
		// Ошибка создания приёмника соединений
		return err
	}

	if printer == nil {
		printer = func(interface{}) {}
	}

	s.stopper = make(chan interface{}, 1)

	s.goroutines_counter.Add(1)

	go func() { // Запускаем параллельную подпрограмму приёма новых соединений
		defer func() { s.goroutines_counter.Done() }()

		// Принимаем входящие соединения и обрабатываем их
		for c, e := s.listener.Accept(); e == nil; c, e = s.listener.Accept() {
			// Соединение принято
			conn := c

			h_sender := func(buf []byte) (int, error) {
				var send_sz int
				var sz int
				var s_err error

				for s_buf := buf; (s_err == nil) && (len(s_buf) > 0); {
					sz, s_err = conn.Write(s_buf)
					if sz > 0 {
						s_buf = s_buf[sz:]
						send_sz += sz
					} else {
						if s_err == nil {
							s_err = io.EOF
						}
					}
				} // for s_buf := buf, (s_err == nil) && (len(s_buf) > 0);

				return send_sz, s_err
			}

			var close_flag int32
			close_notify_chan := make(chan error, 1)

			conn_closer := func() (res error) {
				if !atomic.CompareAndSwapInt32(&close_flag, 0, 1) {
					// Соединение уже закрыто
					return
				}

				// Закрываем соединение
				res = conn.Close()

				// Уведомляем подпрограмму закрытия о том, что соединение уже закрыто
				close(close_notify_chan)

				return
			}
			srv_data_producer := &common_write_closer{h_writer: h_sender, h_closer: conn_closer}
			conn_consumer := dw_maker(srv_data_producer)

			// Запускаем подпрограмму считывания данных из соединения
			s.goroutines_counter.Add(1)
			go func() {
				defer func() {
					s.goroutines_counter.Done()
					// conn_closer()
					printer("Читатель остановлен")
				}()

				printer("Читатель запущен")
				buf := make([]byte, 1000)

				for sz, e := conn.Read(buf); e == nil; sz, e = conn.Read(buf) {
					if sz == 0 {
						// Соединение закрыто удалённой стороной
						e = io.EOF
						break
					}

					// Передача данных потребителю
					readed_buf := buf[:sz]
					printer(readed_buf)
					_, e = conn_consumer.Write(readed_buf)
				}

				// Закрытие потребителя
				printer(e)
				conn_consumer.CloseWithError(e)
				conn_closer()
			}()

			// Запускаем подпрограмму закрытия соединения при остановке сервера
			s.goroutines_counter.Add(1)
			go func() {
				defer func() {
					s.goroutines_counter.Done()
				}()

				for {
					select {
					case <-s.stopper:
						// Закрываем соединение, завершаем подпрограмму
						conn.Close()
						return

					case _, chk := <-close_notify_chan:
						if !chk {
							// Получили уведомление о закрытии соединения
							return
						}
					} // select
				} // for
			}()
		} // for conn, e := s.listener.Accept(); e == nil; conn, e = s.listener.Accept()
	}()

	return nil
} // func (s *Server) Start

// Останавливаем сервер
func (s *Server) Stop() {
	if !atomic.CompareAndSwapInt32(&s.must_be_stopped, 0, 1) {
		return
	}

	// Закрывем "приёмник"
	s.listener.Close()

	// Сообщаем подпрограммам соединений о необходимости завершения
	close(s.stopper)

	// Ждём завершения подпрограмм и выходим
	s.goroutines_counter.Wait()
} // func (s *Server) Stop {

// Функция запуска сервера. Возвращает запущенный сервер и nil в случае успеха, иначе - nil и ошибку
func RunNewServer(host string, port uint16, dw_maker DataWorkersMaker, printer func(msg interface{})) (*Server, error) {
	s := &Server{}
	err := s.start(host, port, dw_maker, printer)
	if err == nil {
		return s, nil
	}

	return nil, err
}
