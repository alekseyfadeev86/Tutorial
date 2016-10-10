package CommonWebServer

import (
	"CommonTcpServer"
	"HttpUtils"
	"WebsocketUtils"
	"errors"
	"io"
)

var (
	ErrEmptyWorker = errors.New("Empty HTTP-worker was set")
)

// Тип обработчика пакетов веб-сокетов
type WsWorker interface {
	// Установка отправителя пакетов и закрыватеоя соединения
	// (метод должен вызываться до передачи первого пакета на обработку)
	Initialize(sender func(f WebsocketUtils.Frame) error, closer io.Closer)

	// Обработка принятого пакета
	Work(f WebsocketUtils.Frame)

	// Закрытие обработчика
	Close() error
}

// Тип обработчика запросов http (если на вход подаётся nil - получен "плохой" запрос),
// если обработчик возвращает HttpUtils.HttpRequest nil - соединение закрывается,
// если возвращает WsWorker не nil - соединение переходит на протокол веб-сокетов,
// предварительно отправив ответ http (предполагается, что подтверждение соединения
// сделано внутри обработчика)
type HttpWorker func(*HttpUtils.HttpRequest) (*HttpUtils.HttpResponse, WsWorker)

// Обработчик данных HTTP
type http_data_consumer struct {
	// Объект для отправки ответов HTTP
	conn io.WriteCloser

	// Обработчик запросов HTTP
	worker HttpWorker

	// Обработчик пакетов веб-сокетов
	ws_worker WsWorker

	// Читатель пакетов веб-сокетов
	ws_parser WebsocketUtils.FrameParser

	// Необработанные данные
	unworked_data []byte
}

func (c *http_data_consumer) Write(buf []byte) (sz int, e error) {
	sz = len(buf)

	var data_to_work []byte
	if len(c.unworked_data) > 0 {
		// Остались необработанные данные
		data_to_work = append(c.unworked_data, buf...)
		c.unworked_data = nil
	} else {
		data_to_work = buf
	}

	for len(data_to_work) > 0 {
		if c.ws_worker == nil {
			// Разбираем данные по протоколу http
			req, left_data, success := HttpUtils.ParseOneReq(data_to_work)

			if (req != nil) || !success {
				// Обрабатываем запрос
				resp, new_ws_worker := c.worker(req)
				if resp != nil {
					// Отправляем ответ
					resp_data := resp.Serialize()
					for len(resp_data) > 0 {
						s, err := c.conn.Write(resp_data)
						if err != nil {
							// Ошибка отправки
							e = err
							return
						} else if s > 0 {
							resp_data = resp_data[s:]
						} else {
							panic("Write returns 0, nil")
						}
					}
				} else {
					// Закрываем соединение
					c.conn.Close()
					return
				}

				if new_ws_worker != nil {
					sender := func(f WebsocketUtils.Frame) error {
						_, e := c.conn.Write(f.Serialize())
						return e
					}
					new_ws_worker.Initialize(sender, c.conn)
					c.ws_worker = new_ws_worker
				}
			} // if (req != nil) || !success

			l := len(data_to_work)
			if l == len(left_data) {
				// Уже всё обработали
				if l > 0 {
					// Остались необработанные данные
					c.unworked_data = left_data
				}
				break
			}
			data_to_work = left_data
		} else { // if c.ws_worker == nil
			// Используем протокол веб-сокетов
			c.ws_parser.Write(data_to_work)
			data_to_work = nil
			frames := c.ws_parser.Parse()
			for _, f := range frames {
				c.ws_worker.Work(f)
			}
		}
	} // for len(data_to_work) > 0

	return
} // func (c *http_data_consumer) Write(buf []byte) (sz int, e error)

func (c *http_data_consumer) Close() error {
	if c.ws_worker != nil {
		c.ws_worker.Close()
		c.ws_worker = nil
	}

	return c.conn.Close()
}

func (c *http_data_consumer) CloseWithError(e error) error {
	return c.Close()
}

func make_factory(worker HttpWorker) CommonTcpServer.DataWorkersMaker {
	if worker == nil {
		return nil
	}

	return func(conn io.WriteCloser) CommonTcpServer.DataConsumer {
		if conn == nil {
			return nil
		}

		return &http_data_consumer{conn: conn, worker: worker}
	}
}

// Функция запуска сервера. Возвращает запущенный сервер и nil в случае успеха, иначе - nil и ошибку
func RunNewServerEx(host string, port uint16, worker HttpWorker, printer func(msg interface{})) (srv io.Closer, err error) {
	w := make_factory(worker)
	if w == nil {
		err = ErrEmptyWorker
	} else {
		var s *CommonTcpServer.Server
		s, err = CommonTcpServer.RunNewServer(host, port, w, 10*1024, 10000, printer)

		if s != nil {
			srv = s
		}
	}

	return
}

func RunNewServer(host string, port uint16, worker HttpWorker) (srv io.Closer, err error) {
	return RunNewServerEx(host, port, worker, nil)
}
