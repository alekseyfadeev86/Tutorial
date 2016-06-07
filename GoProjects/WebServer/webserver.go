package main

// Интерфейс абстрактного работника с возможностью формироваиня нового
type WorkerReplacer interface {
	// Метод обработки данных
	// Результат: 1) надо ли продолжать работу 2) новый "работник"
	WorkData(buf []byte, sender func([]byte)) (bool, WorkerReplacer)

	// Метод обработки ошибок; если возвращает false - соединение нужно закрыть
	OnError(err error) bool
}

// Тип обработчик http: принимает запрос, возвращает ответ на запрос, новый обработчик и признак, надо ли продолжать общение
type HttpHandler func(req *HttpRequest) (*HttpResponse, WorkerReplacer, bool)

// Структура "работника", работающего по протоколу HTTP/1.1
type HttpWorker struct {
	// Читатель запросов http
	reader RequestsReader

	requests_worker HttpHandler
}

func (w *HttpWorker) WorkData(buf []byte, sender func([]byte)) (bool, WorkerReplacer) {
	if w.requests_worker == nil {
		return false, nil
	}

	w.reader.OnRead(buf)

	// Здесь делается допущение, что клиент
	// 100% не будет отправлять следующий запрос,
	// не дождавшись ответа на предыдущий
	req := w.reader.ParseOne()
	if req != nil {
		resp, replacer, go_on := w.requests_worker(req)
		if resp != nil {
			sender(resp.Serialize())
		} else {
			go_on = false
		}

		return go_on, replacer
	}

	return true, nil
} // func (w *HttpWorker) WorkData

func (w *HttpWorker) OnError(err error) bool {
	return false
}

// Обработчики протокола веб-сокетов
type WebsockHandlers struct {
	// Обработчик входящих данных
	InputHandler func(i *Frame)

	// Отправитель (должен быть запущет в отдельной продпрограмме)
	Writer func(sender func([]byte))
}

type WebWorker struct {
	// Текущий "работник" (может работать по http, либо веб-сокетам)
	current_worker WorkerReplacer

	// Функция отправки данных
	data_sender func([]byte)
}

func (w *WebWorker) WorkData(buf []byte) bool {
	if w.current_worker == nil {
		return false
	}

	go_on, new_worker := w.current_worker.WorkData(buf, w.data_sender)

	if !go_on {
		return false
	} else if new_worker != nil {
		w.current_worker = new_worker
	}

	return true
}

func (w *WebWorker) OnError(err error) bool {
	if w.current_worker != nil {
		return w.current_worker.OnError(err)
	} else {
		return err == nil
	}
}

// Функция-создатель "фабрик" обработчиков
func WebWorkersFactoryMaker(h_http HttpHandler) DataWorkersMaker {
	if h_http == nil {
		panic("Http handler cannot be nil!")
	}

	res := func(sender func([]byte)) DataWorker {
		return &WebWorker{current_worker: &HttpWorker{requests_worker: h_http}, data_sender: sender}
	}

	return res
}
