package main

import (
	"fmt"
	"net"
	"strconv"
	"sync"
)

func DebugPrinter(s string) {
	fmt.Println(s)
}

type Printer func(data_to_print string)

// Интерфейс обработчика данных
type DataWorker interface {
	// Метод обработки данных; если возвращает false - соединение нужно закрыть
	WorkData(buf []byte) bool

	// Метод обработки ошибок; если возвращает false - соединение нужно закрыть
	OnError(err error) bool
}

// Тип функции-создателя обработчиков данных
type DataWorkersMaker func(sender func([]byte)) DataWorker

func tcp_server(host string,
	port uint16,
	data_workers_maker DataWorkersMaker,
	printer Printer,
	stopper <-chan interface{}) (err error) {

	if printer == nil {
		printer = func(s string) {}
	}

	port_str := strconv.Itoa(int(port))
	full_host := host + ":" + port_str

	// Создаём приёмник соединений
	listener, err := net.Listen("tcp", full_host)
	if err == nil {
		printer("Запуск сервера на адресе " + full_host)
	} else {
		printer("Ошибка запуска сервера на адресе " + full_host + ":" + err.Error())
		return
	}

	// Канал, в который поступят данные в случае завершения подпрограммы-приёмника соединений
	listener_stop_notifier := make(chan interface{}, 1)

	goroutines_stopper := make(chan interface{}, 1)

	// Счётчик подпрограмм обработки соединений
	var goroutines_counter sync.WaitGroup

	go func() { // Запускаем параллельную подпрограмму приёма новых соединений
		defer func() { listener_stop_notifier <- byte(0) }()

		for {
			// Ждём входящее соединение
			conn, e := listener.Accept()

			if e != nil {
				// Завершаем приём новых соединений
				break
			}

			c := make(chan []byte, 100)
			sender := func(data []byte) {
				defer func() { recover() }() // На случай, если канал будет закрыт
				c <- data
			}
			w := data_workers_maker(sender)

			goroutines_counter.Add(1)
			go func() {
				defer func() {
					close(c)
					goroutines_counter.Done()
					printer("Читатель остановлен")
				}()

				printer("Читатель запущен")
				buf := make([]byte, 100)

				for {
					sz, e := conn.Read(buf)

					if e == nil {
						printer(string(buf[:sz]))
						if !w.WorkData(buf[:sz]) {
							break
						}
					} else if !w.OnError(e) {
						break
					}
				}
			}()

			goroutines_counter.Add(1)
			go func() {
				defer func() {
					goroutines_counter.Done()
					conn.Close()
					printer("Писатель остановлен")
				}()

				printer("Писатель запущен")
				for {
					select {
					case <-goroutines_stopper:
						// Закрываем соединение, завершаем подпрограмму
						return

					case buf := <-c:
						if buf == nil {
							return
						}

						for len(buf) > 0 {
							sz, e := conn.Write(buf)

							if (sz > 0) && (e == nil) {
								printer(string(buf[:sz]))
								buf = buf[sz:]
							} else if !w.OnError(e) {
								return
							} else {
								break
							}
						}
					} // select
				} // for
			}()
		} // for
	}()

	// Ждём команды на остановку
	<-stopper

	// Закрывем "приёмник"
	listener.Close()

	// Ждём уведомления о завершении подпрограммы-приёмника соединений
	<-listener_stop_notifier
	close(listener_stop_notifier)

	// Сообщаем подпрограммам соединений о необходимости завершения
	close(goroutines_stopper)

	// Ждём завершения подпрограмм и выходим
	goroutines_counter.Wait()
	return
} // func tcp_server
