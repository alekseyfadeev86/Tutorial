package main

/*
Тут будут утилиты http, например, формирование/разбор пакетов HTTP и веб-сокетов.
Пока велосипедостроение, затем - использование всей мощи go
*/

import (
	"strconv"
	"strings"
)

const (
	// Окончание одной строки заголовка http-запроса
	// Заголовок запроса отделяется от тела двойным окончанием
	endline_str = "\r\n"

	// Текущая поддерживаемая версия http
	http_version = "HTTP/1.1"

	// Разделитель имени параметра и его значения
	param_splitter = ": "

	// Имя параметра запроса/ответа, содержащего длину тела
	body_size_param_name = "Content-Length"
)

// Тип параметра заголовка запроса/ответа
type Param struct {
	Name, Value string
}

func (p Param) Serialize() string {
	return strings.Join([]string{p.Name, p.Value}, param_splitter)
}

func param_parser(data string) Param {
	parts := strings.Split(data, param_splitter)
	if len(parts) > 1 {
		return Param{Name: parts[0], Value: parts[1]}
	} else {
		return Param{Name: parts[0]}
	}
}

// Базовая структура для запроса и ответа
type req_resp struct {
	// Параметры заголовка
	HeaderParams []Param

	// Тело
	Body []byte
}

// Тип запроса HTTP
type HttpRequest struct {
	req_resp

	// Тип запроса (GET, POST, ...) и хост
	Type, Host string
}

func (req *HttpRequest) Serialize() []byte {
	header_strs := make([]string, len(req.HeaderParams)+2)
	header_strs[0] = strings.Join([]string{req.Type, req.Host, http_version}, " ")
	header_strs[len(header_strs)-1] = endline_str

	for i, p := range req.HeaderParams {
		header_strs[i+1] = strings.Join([]string{p.Name, param_splitter, p.Value}, "")
	}

	return append([]byte(strings.Join(header_strs, endline_str)), req.Body...)
}

func make_request(req_type string, host string, head_params []Param, body []byte) HttpRequest {
	return HttpRequest{Type: req_type, Host: host, req_resp: req_resp{HeaderParams: head_params, Body: body}}
}

// Структура разбора строк запросов
type RequestsReader struct {
	// Необработанные данные
	unworked_data []byte

	// Незаконченный объект запроса (незаполненное тело)
	unfinished_req *HttpRequest

	// Размер недостающих данных для формирования тела объекта
	left_data_sz uint64
}

// Добавление новых данных
func (reader *RequestsReader) OnRead(data []byte) {
	reader.unworked_data = append(reader.unworked_data, data...)
} // func (reader *RequestsReader) OnRead(data []byte)

// Поиск среди считанных данных первой строки запроса
// и отсечение обработанных данных.
// Возвращает тип, хост и успешность поиска
func (reader *RequestsReader) get_req_type_host() (string, string, bool) {
	req_identifier := http_version + endline_str
	req_id_len := len(req_identifier)

	// Ищем среди считанных данных признак заголовка запроса вида Тип Хост HTTP/1.1\r\n
	for len(reader.unworked_data) > 0 {
		s_data := string(reader.unworked_data)

		pos2 := strings.Index(s_data, req_identifier)
		is_found := pos2 >= 0
		if pos2 < 0 {
			// В данных отсутствует идентификатор текущей версии протокола
			pos2 = len(reader.unworked_data)
		} // if pos2 < 0

		// Отсекаем данные от начала и до последнего признака окончания строки
		// (включая его), встреченного до pos2
		if pos1 := strings.LastIndexAny(s_data[:pos2], endline_str); pos1 >= 0 {
			// Идентификатор конца строки встречается до идентификатора - отрезаем
			pos1++
			if pos1 < len(reader.unworked_data) {
				reader.unworked_data = reader.unworked_data[pos1:]
			} else {
				reader.unworked_data = nil
			}

			s_data = string(reader.unworked_data)
			pos2 -= pos1
		}

		if !is_found {
			// Признак текущей версии протокола найден не был
			break
		}

		head_params := strings.Split(s_data[:pos2], " ")

		// Отсекаем обработанные данные
		pos2 += req_id_len
		if pos2 >= len(reader.unworked_data) {
			// Просмотрели все считанные данные
			reader.unworked_data = nil
		} else {
			// Отсекаем ту часть считанных данных, которую обработали
			reader.unworked_data = reader.unworked_data[pos2:]
		}

		if (len(head_params) == 3) && (head_params[2] == "") {
			// Получили тип запроса и хост
			return head_params[0], head_params[1], true
		}
	} // for len(reader.unworked_data) > 0

	return "", "", false
} // func (reader *RequestsReader) get_req_type_host() (string, string, bool)

// Формирование одного объекта запроса из прочитанных данных
// если сформировать не получается - возвращаем nil
func (reader *RequestsReader) ParseOne() (res *HttpRequest) {
	if reader.unfinished_req == nil {
		// Начинаем формировать новый объект запроса: ищем первую строку запроса вида Тип Хост HTTP/1.1/r/n
		if len(reader.unworked_data) == 0 {
			// Нечего обрабатывать
			return
		}

		req_type, req_host, is_found := reader.get_req_type_host()
		if is_found {
			// Получили тип запроса и хост
			reader.unfinished_req = &HttpRequest{Type: req_type, Host: req_host}
			reader.left_data_sz = 0
		} else {
			// Первая строка заголовка запроса не была найдена
			return
		}
	} // if reader.unfinished_req == nil

	if reader.left_data_sz == 0 {
		// Параметры запроса ещё не сформированы
		s_data := string(reader.unworked_data)
		if pos := strings.Index(s_data, endline_str+endline_str); pos >= 0 {
			// Обнаружен разделитель между заголовком и телом
			s_data = s_data[:pos]
			reader.unworked_data = reader.unworked_data[pos+(2*len(endline_str)):]
		} else {
			// Разделитель между заголовком и телом не обнаружен
			return
		}

		// Разбираем часть заголовка с параметрами
		var body_len int = -1 // Размер тела запроса (пока не обнаружен)
		param_strs := strings.Split(s_data, endline_str)
		reader.unfinished_req.HeaderParams = make([]Param, 0, len(param_strs))
		for _, p_str := range param_strs {
			one_param := param_parser(p_str)
			if one_param.Name == body_size_param_name {
				// Обнаружили параметр длины тела
				body_len, _ = strconv.Atoi(one_param.Value)
			}
			reader.unfinished_req.HeaderParams = append(reader.unfinished_req.HeaderParams, one_param)
		}

		// Формируем тело запроса
		data_len := len(reader.unworked_data)
		if body_len < 0 {
			// Размер тела не известен - тупо записываем в него остаток считанных данных
			body_len = data_len
		}

		if body_len <= data_len {
			// Считано достаточно данных для формирования тела запроса
			reader.unfinished_req.Body = reader.unworked_data[:body_len]

			reader.unworked_data = reader.unworked_data[body_len:]
			if len(reader.unworked_data) == 0 {
				reader.unworked_data = nil
			}

			res = reader.unfinished_req
			reader.unfinished_req = nil
			reader.left_data_sz = 0
		} else {
			// Считанных данных не достаточно для формирования всего тела
			if cap(reader.unworked_data) >= body_len {
				// Ёмкости среза тела запроса достаточно, чтобы вместить все данные
				reader.unfinished_req.Body = reader.unworked_data
			} else {
				// Ёмкости среза тела запроса не достаточно, чтобы вместить все данные - формируем новый срез
				reader.unfinished_req.Body = append(make([]byte, 0, body_len), reader.unworked_data...)
			}

			reader.unworked_data = nil
			reader.left_data_sz = uint64(body_len - data_len)
		}
		// if reader.left_data_sz == 0
	} else {
		// Формируем тело запроса
		var cut_len uint64 = uint64(len(reader.unworked_data))
		if cut_len > reader.left_data_sz {
			cut_len = reader.left_data_sz
		}

		// Добавляем полученные данные в конец тела запроса
		reader.unfinished_req.Body = append(reader.unfinished_req.Body, reader.unworked_data[:cut_len]...)

		reader.unworked_data = reader.unworked_data[cut_len:]
		if len(reader.unworked_data) == 0 {
			reader.unworked_data = nil
		}

		reader.left_data_sz -= cut_len
		if reader.left_data_sz == 0 {
			// Тело запроса полностью сформировано, запрос готов
			res = reader.unfinished_req
			reader.unfinished_req = nil
		}
	}

	return
} // func (reader *RequestsReader) ParseOne() *HttpRequest

// Структура ответа
type HttpResponse struct {
	req_resp

	// Код ответа
	Code uint16

	// Описание ответа
	What string
}

func (resp *HttpResponse) Serialize() []byte {
	header_strs := make([]string, len(resp.HeaderParams)+2)
	header_strs[0] = strings.Join([]string{http_version, strconv.Itoa(int(resp.Code)), resp.What}, " ")
	header_strs[len(header_strs)-1] = endline_str

	for i, p := range resp.HeaderParams {
		header_strs[i+1] = strings.Join([]string{p.Name, param_splitter, p.Value}, "")
	}

	header := strings.Join(header_strs, endline_str)

	return append([]byte(header), resp.Body...)
}

func MakeHttpResponse(code uint16, what string, header_params []Param, body []byte) *HttpResponse {
	return &HttpResponse{req_resp: req_resp{HeaderParams: header_params, Body: body}, Code: code, What: what}
}
