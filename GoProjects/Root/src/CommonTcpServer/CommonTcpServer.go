package CommonTcpServer

import (
	"io"
	"net"
	"strconv"
	"sync"
	"sync/atomic"
)

// Интерфейс обработчика данных
type DataWorker interface {
	// Метод обработки данных; если возвращает false - соединение нужно закрыть
	WorkData(buf []byte) bool

	// Метод обработки ошибок; если возвращает false - соединение нужно закрыть
	OnError(err error) bool
}

// Тип функции-создателя обработчиков данных
type DataWorkersMaker func(sender func([]byte)) DataWorker

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

		for {
			// Ждём входящее соединение
			conn, e := s.listener.Accept()

			if e != nil {
				// Завершаем приём новых соединений
				break
			}

			// Создаём канал для передачи данных на отправку по новому соединению
			c := make(chan []byte, 100)

			sender := func(data []byte) {
				defer func() { recover() }() // На случай, если канал будет закрыт
				c <- data
			}
			w := dw_maker(sender)

			s.goroutines_counter.Add(1)
			go func() {
				defer func() {
					close(c)
					s.goroutines_counter.Done()
					printer("Читатель остановлен")
				}()

				printer("Читатель запущен")
				buf := make([]byte, 100)

				for {
					sz, e := conn.Read(buf)

					if atomic.LoadInt32(&s.must_be_stopped) != 0 {
						// Сервер прекращает работу
						break
					}

					if e == nil {
						printer(buf[:sz])
						if !w.WorkData(buf[:sz]) {
							break
						}
					} else {
						printer(e)
						if !w.OnError(e) || (e == io.EOF) {
							break
						}
					}
				}
			}()

			s.goroutines_counter.Add(1)
			go func() {
				defer func() {
					s.goroutines_counter.Done()
					conn.Close()
					printer("Писатель остановлен")
				}()

				printer("Писатель запущен")
				for {
					select {
					case <-s.stopper:
						// Закрываем соединение, завершаем подпрограмму
						return

					case buf := <-c:
						if buf == nil {
							return
						}

						for len(buf) > 0 {
							sz, e := conn.Write(buf)

							if (sz > 0) && (e == nil) {
								printer(buf[:sz])
								buf = buf[sz:]
								continue
							} else if e != nil {
								printer(e)
								if !w.OnError(e) || (e == io.EOF) {
									return
								}
							}

							break
						} // for len(buf) > 0
					} // select
				} // for
			}()
		} // for
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
