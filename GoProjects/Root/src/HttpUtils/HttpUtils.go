package HttpUtils

import (
	"IoUtils"
	"bytes"
	"errors"
	"io"
	"strconv"
	"strings"
)

const (
	// Окончание одной строки заголовка http-запроса
	// Заголовок запроса отделяется от тела двойным окончанием
	endline_str = "\r\n"

	// Текущая поддерживаемая версия http
	http_version = "HTTP/1.0"

	// Разделитель имени параметра и его значения
	param_splitter = ": "

	// Имя параметра запроса/ответа, содержащего длину тела
	BodySizeParamName = "Content-Length"
)

// Тип параметра заголовка запроса/ответа
type HeaderParam struct {
	// Имя и значение параметра
	Name, Value string
}

// Формирует строку параметра вида <имя параметра>: <значение параметра>
func (p HeaderParam) Serialize() string {
	return strings.Join([]string{p.Name, p.Value}, param_splitter)
}

// Разбитает строку вида <имя параметра>: <значение параметра> и формирует объект HeaderParam
// Результат - структура с именеи и значением + успешность операции
func param_parser(data string) (HeaderParam, bool) {
	parts := strings.Split(data, param_splitter)
	if len(parts) > 1 {
		return HeaderParam{Name: parts[0], Value: parts[1]}, true
	} else {
		return HeaderParam{}, false
	}
}

// Базовая структура для запроса и ответа
type req_resp struct {
	// Параметры заголовка
	HeaderParams []HeaderParam

	// Тело
	Body []byte
}

// Тип запроса HTTP
type HttpRequest struct {
	req_resp

	// Тип запроса (GET, POST, ...) и хост
	Type, Host string
}

// Формирование запроса http в виде набора байт для отправки по сети
func (req *HttpRequest) Serialize() []byte {
	header_strs := make([]string, len(req.HeaderParams)+2)
	header_strs[0] = strings.Join([]string{req.Type, req.Host, http_version}, " ")
	header_strs[len(header_strs)-1] = endline_str

	for i, p := range req.HeaderParams {
		header_strs[i+1] = strings.Join([]string{p.Name, param_splitter, p.Value}, "")
	}

	header := strings.Join(header_strs, endline_str)

	return append([]byte(header), req.Body...)
}

// Формирование структуры запроса http
// req_type, req_host - тип (GET, POST,...) и хост (например, /index.html)
// head_params - параметры заголовка
// body - тело запроса
func MakeRequest(req_type, req_host string, head_params []HeaderParam, body []byte) HttpRequest {
	return HttpRequest{req_resp: req_resp{HeaderParams: head_params, Body: body}, Type: req_type, Host: req_host}
}

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

func MakeResponse(code uint16, what string, header_params []HeaderParam, body []byte) HttpResponse {
	return HttpResponse{req_resp: req_resp{HeaderParams: header_params, Body: body}, Code: code, What: what}
}

// Функция разбора входящих данных и формирования объектов req_resp
// Возвращаемый результат: сформированный объект (при наличии), необработанные данные,
// успешность выполнения (если false - получен некорректный запрос)
func parse_one_req_resp(data []byte) (rr *req_resp, unworked_data []byte, success bool) {
	unworked_data = data
	success = true

	endl := []byte(endline_str) // Конец строки
	rr = &req_resp{}

	// Формируем параметры запроса
	pos := bytes.Index(unworked_data, endl)
	for ; pos >= 0; pos = bytes.Index(unworked_data, endl) {
		one_param_str := string(unworked_data[:pos])
		unworked_data = unworked_data[pos+len(endl):]

		if pos == 0 {
			// Дошли до конца заголовка
			break
		}

		// Обрабатываем параметр заголовка
		param, succ := param_parser(one_param_str)
		if succ {
			// Обработали один параметр
			rr.HeaderParams = append(rr.HeaderParams, param)
		} else {
			// Ошибка разбора параметра заголовка
			rr = nil
			success = false
			return
		}
	} // for ; pos >= 0; pos = bytes.Index(parser.unworked_data, endl)

	if pos < 0 {
		// Символ перехода на следующую строку не найден
		rr = nil
		unworked_data = data
		return
	}

	// Если попали сюда - параметры заголовка считаны
	var body_len int = -1
	for _, param := range rr.HeaderParams {
		if param.Name == BodySizeParamName {
			// Обнаружили параметр длины тела
			var e error
			body_len, e = strconv.Atoi(param.Value)
			if e != nil {
				// Некорректное значение параметра длины
				rr = nil
				success = false
				return
			}

			break
		}
	}

	if body_len < 0 {
		// В параметрах не содержится длина тела - тупо записываем в него всё, что успели прочитать
		body_len = len(unworked_data)
	}

	if len(unworked_data) < body_len {
		// Недостаточно данных для формирования запроса
		rr = nil
		unworked_data = data
		return
	}

	// Формируем тело запроса
	rr.Body = unworked_data[:body_len]
	unworked_data = unworked_data[body_len:]
	return
} // func parse_one_req_resp(data []byte) (rr *req_resp, unworked_data []byte, success bool)

// Функция разбора входящих данных и формирования объектов HttpRequest
// Возвращаемый результат: сформированный запрос (при наличии), необработанные данные,
// успешность выполнения (если false - получен некорректный запрос)
func ParseOneReq(data []byte) (req *HttpRequest, unworked_data []byte, success bool) {
	unworked_data = data
	success = true

	endl := []byte(endline_str)                          // Конец строки
	req_identifier := []byte(http_version + endline_str) // Признак запроса (версия HTTP + символы конца строки)

	// Ищем заголовок
	p_req_id := bytes.Index(unworked_data, req_identifier)
	p_endl := -1
	if p_req_id >= 0 {
		p_endl = bytes.LastIndex(unworked_data[:p_req_id], endl)
	} else {
		p_endl = bytes.LastIndex(unworked_data, endl)
	}

	if p_endl >= 0 {
		// Обнаружен переход на следующую строку перед признаком запроса - косяк
		unworked_data = unworked_data[p_endl+len(endline_str):]
		success = false
		return
	} else if p_req_id >= 0 {
		// Найден признак запроса
		p_req_id += len(req_identifier)
		params := strings.Split(string(unworked_data[:p_req_id]), " ")
		unworked_data = unworked_data[p_req_id:]

		if len(params) != 3 {
			// Неверный заголовок
			success = false
			return
		} // if len(params) != 3

		req = &HttpRequest{Type: params[0], Host: params[1]}
	} else {
		// Не нашли ни признака запроса, ни перехода на следующую строку
		unworked_data = data
		return
	}

	// Формируем параметры и тело запроса
	var rr *req_resp
	rr, unworked_data, success = parse_one_req_resp(unworked_data)
	if rr != nil {
		req.req_resp = *rr
	} else {
		req = nil
		if success {
			unworked_data = data
		}
	}

	return
} // func ParseOneReq(data []byte) (req *HttpRequest, unworked_data []byte, success bool)

// Функция разбора входящих данных и формирования объектов HttpResponse
// Возвращаемый результат: сформированный запрос (при наличии), необработанные данные,
// успешность выполнения (если false - получен некорректный запрос)
func ParseOneResp(data []byte) (resp *HttpResponse, unworked_data []byte, success bool) {
	unworked_data = data
	success = true

	endl := []byte(endline_str) // Конец строки
	b_http_ver := []byte(http_version)

	// Ищем заголовок
	p_resp_id := bytes.Index(unworked_data, b_http_ver)
	if p_resp_id > 0 {
		// Перед ответом какие-от левые данные
		success = false
		unworked_data = unworked_data[p_resp_id:]
		return
	}

	// Ищем конец строки
	p_endl := bytes.Index(unworked_data, endl)
	if p_endl < 0 {
		// Переход на следующую строку не найден - недостаточно данных для формирования запроса
		unworked_data = data
		return
	} else if p := bytes.LastIndex(unworked_data[:p_endl], b_http_ver); p > 0 {
		// В этой же строке обнаружен ещё как минимум один признак заголовка
		unworked_data = unworked_data[p:]
		success = false
		return
	}

	// Разбираем первую строку
	head_line := string(unworked_data[len(http_version):p_endl])
	unworked_data = unworked_data[p_endl+len(endl):]
	if head_line[0] != ' ' {
		// После версии HTTP идёт не пробел
		success = false
		return
	}

	head_line = head_line[1:]
	var params [2]string
	if p := strings.Index(head_line, " "); p > 0 {
		params[0] = head_line[:p]
		params[1] = head_line[p+1:]
	} else {
		params[0] = head_line
	}

	if code, err := strconv.Atoi(params[0]); err == nil {
		// Успех
		resp = &HttpResponse{Code: uint16(code), What: params[1]}
	} else {
		// Некорректная первая строка заголовка
		success = false
		return
	}

	// Формируем параметры и тело запроса
	var rr *req_resp
	rr, unworked_data, success = parse_one_req_resp(unworked_data)
	if rr != nil {
		resp.req_resp = *rr
	} else {
		resp = nil
		if success {
			unworked_data = data
		}
	}

	return
} // func ParseOneResp(data []byte) (resp *HttpResponse, unworked_data []byte, success bool)

// Базовая структура для запроса и ответа
type big_req_resp struct {
	// Параметры заголовка
	HeaderParams []HeaderParam

	// Тело
	Body io.Reader
}

// Тип большого запроса HTTP
type HttpBigRequest struct {
	big_req_resp

	// Тип запроса (GET, POST, ...) и хост
	Type, Host string
}

// Формирование структуры большого запроса http
// req_type, req_host - тип (GET, POST,...) и хост (например, /index.html)
// head_params - параметры заголовка
// body - тело запроса
func MakeBigRequest(req_type, req_host string, head_params []HeaderParam, body io.Reader) HttpBigRequest {
	return HttpBigRequest{big_req_resp: big_req_resp{HeaderParams: head_params, Body: body}, Type: req_type, Host: req_host}
}

// Формирование большого запроса http в виде набора байт для отправки по сети
func SerializeBigRequest(req_type, req_host string, head_params []HeaderParam, body io.Reader) io.Reader {
	header_strs := make([]string, len(head_params)+2)
	header_strs[0] = strings.Join([]string{req_type, req_host, http_version}, " ")
	header_strs[len(header_strs)-1] = endline_str

	for i, p := range head_params {
		header_strs[i+1] = p.Serialize()
	}

	header := strings.Join(header_strs, endline_str)

	return IoUtils.Serialize([]byte(header), body, nil, nil)
}

// Структура большого ответа
type HttpBigResponse struct {
	big_req_resp

	// Код ответа
	Code uint16

	// Описание ответа
	What string
}

// Формирование структуры большого запроса http
// code, what - код ответа (например, 200) и описание (например, OK)
// head_params - параметры заголовка
// body - тело запроса
func MakeBigResponse(code uint16, what string, head_params []HeaderParam, body io.Reader) HttpBigResponse {
	return HttpBigResponse{big_req_resp: big_req_resp{HeaderParams: head_params, Body: body}, Code: code, What: what}
}

func SerializeBigResponse(resp_code uint16, resp_what string, head_params []HeaderParam, body io.Reader) io.Reader {
	header_strs := make([]string, len(head_params)+2)
	header_strs[0] = strings.Join([]string{http_version, strconv.Itoa(int(resp_code)), resp_what}, " ")
	header_strs[len(header_strs)-1] = endline_str

	for i, p := range head_params {
		header_strs[i+1] = p.Serialize()
	}

	header := strings.Join(header_strs, endline_str)

	return IoUtils.Serialize([]byte(header), body, nil, nil)
}

// Структура разбора больших запросов в виде байт (потоконебезопасная)
type BigRequestsParser struct {
	// Необработанные данные
	unworked_data []byte

	// Незаконченный объект запроса (не заполнены параметры заголовка)
	unfinished_req *HttpBigRequest

	// Отправитель тела
	body_writer io.WriteCloser
}

// Обработка новых данных
func (parser *BigRequestsParser) Write(data []byte) (data_len int, err error) {
	data_len = len(data)

	if parser.body_writer != nil {
		// Есть незаполненное тело предыдущего запроса
		if (parser.unworked_data != nil) || (parser.unfinished_req != nil) {
			// Есть куда отправлять тело запроса, но есть необработанные данные и/или незаконченный запрос
			panic(errors.New("(parser.unworked_data != nil) || (parser.unfinished_req != nil)"))
		}

		for sz, e := parser.body_writer.Write(data); len(data) > 0; sz, e = parser.body_writer.Write(data) {
			if sz > 0 {
				// Данные (все или часть) были записаны
				if (sz < len(data)) && (e == nil) {
					// Были записаны не все данные, но ошибка нулевая
					panic(errors.New("(sz < len(data)) && (e == nil)"))
				}

				data = data[sz:]
			}

			if e != nil {
				// Произошла ошибка записи - считаем, что всё тело записано
				parser.body_writer.Close()
				parser.body_writer = nil
				break
			}
		}
	} // if parser.body_writer != nil

	// Записываем необработанные данные в буфер для дальнейшей обработки
	parser.unworked_data = append(parser.unworked_data, data...)

	return
} // func (p *BigRequestsParser) Write(data []byte) (int, error)

func (parser *BigRequestsParser) Close() (e error) {
	if parser.body_writer != nil {
		e = parser.body_writer.Close()
		parser.body_writer = nil
	}

	return
}

// func (p *BigRequestsParser) GetRequests() []*HttpBigRequest {
// 	res := p.requests
// 	p.requests = nil
// 	return res
// }

func (parser *BigRequestsParser) Parse() (res []*HttpBigRequest) {

	if (parser.body_writer != nil) && (len(parser.unworked_data) != 0) {
		panic(errors.New("(parser.body_writer != nil) && (len(parser.unworked_data) != 0)"))
	}

	req_identifier := []byte(http_version + endline_str) // Признак запроса (версия HTTP + символы конца строки)
	endl := []byte(endline_str)

	for len(parser.unworked_data) > 0 {
		if parser.unfinished_req == nil {
			// Обрабатываем накопленные данные как начало нового запроса
			p_req_id := bytes.Index(parser.unworked_data, req_identifier)
			p_endl := -1
			if p_req_id >= 0 {
				p_endl = bytes.LastIndex(parser.unworked_data[:p_req_id], endl)
			} else {
				p_endl = bytes.LastIndex(parser.unworked_data, endl)
			}

			if p_req_id < 0 {
				// Признак запроса не был обнаружен
				if p_endl > 0 {
					// ...но был обнаружен переход на новую строку - ошибка
					parser.unworked_data = parser.unworked_data[p_endl+len(endline_str):]
					if len(parser.unworked_data) == 0 {
						parser.unworked_data = nil
					}

					res = append(res, nil)
				}

				return
			} else {
				p_req_id += len(req_identifier)
			}

			if p_endl < 0 {
				p_endl = 0
			} else {
				// Обнаружен переход на новую строку перед признаком запроса
				res = append(res, nil)
				p_endl += len(endline_str)
			}

			// Найден признак запроса
			req_head_line := string(parser.unworked_data[p_endl:p_req_id])
			parser.unworked_data = parser.unworked_data[p_req_id:]
			if len(parser.unworked_data) == 0 {
				parser.unworked_data = nil
			}

			params := strings.Split(req_head_line, " ")
			if len(params) == 3 {
				// Нашли заголовок запроса
				parser.unfinished_req = &HttpBigRequest{Type: params[0], Host: params[1]}
			} else {
				// Неверный заголовок
				res = append(res, nil)
				continue
			}
		} // if parser.unfinished_req == nil

		// Формируем параметры заголовка
		param_error := false
		pos := bytes.Index(parser.unworked_data, endl)
		for ; pos >= 0; pos = bytes.Index(parser.unworked_data, endl) {
			// Формируем строку с параметром (без символа перехода на следующую строку)
			// и отрезаем обработанный кусок данных от необработанных данных (вместе с символом перехода)
			one_param_str := string(parser.unworked_data[:pos])
			parser.unworked_data = parser.unworked_data[pos+len(endl):]
			if len(parser.unworked_data) == 0 {
				parser.unworked_data = nil
			}

			if pos == 0 {
				// Дошли до конца заголовка
				break
			}

			// Обрабатываем параметр заголовка
			param, success := param_parser(one_param_str)
			if success {
				// Обработали один параметр - добавляем его е запросу
				parser.unfinished_req.HeaderParams = append(parser.unfinished_req.HeaderParams, param)
			} else {
				// Ошибка разбора параметра заголовка
				parser.unfinished_req = nil
				res = append(res, nil)
				param_error = true
				break
			}
		} // for ; pos >= 0; pos = bytes.Index(parser.unworked_data, endl)

		if param_error {
			// При разборке параметров заголовка произошла ошибка
			continue
		} else if pos < 0 {
			// Символ перехода на следующую строку не найден - больше обрабатывать нечего
			return
		}

		// Если попали сюда - параметры заголовка считаны полностью,
		// дальше идёт тело и/или следующий запрос
		var body_len int = len(parser.unworked_data)
		for _, param := range parser.unfinished_req.HeaderParams {
			if param.Name == BodySizeParamName {
				// Обнаружили параметр длины тела
				var e error
				body_len, e = strconv.Atoi(param.Value)
				if e != nil {
					// Некорректное значение параметра длины
					parser.unfinished_req = nil
					res = append(res, nil)
					param_error = true
				}

				break
			} // if param.Name == BodySizeParamName
		} // for _, param := range parser.unfinished_req.HeaderParams

		if param_error {
			continue
		}

		if body_len <= len(parser.unworked_data) {
			// В необработанных данных уже содержится всё тело запроса
			parser.unfinished_req.Body = bytes.NewReader(parser.unworked_data[:body_len])
			parser.unworked_data = parser.unworked_data[body_len:]
			if len(parser.unworked_data) == 0 {
				parser.unworked_data = nil
			}
		} else if body_len != 0 {
			// Всё тело ещё не считали
			var one_piece_max_sz int = body_len
			var chan_sz uint16 = 2

			if one_piece_max_sz > 10240 {
				one_piece_max_sz = 10240
				chan_sz = 10
			}

			reader, writer := IoUtils.NonBlockingPipe(one_piece_max_sz, chan_sz)
			parser.body_writer = IoUtils.LimitWriteCloser(writer, int64(body_len))
			parser.unfinished_req.Body = io.LimitReader(reader, int64(body_len))

			if parser.unworked_data != nil {
				sz, e := parser.body_writer.Write(parser.unworked_data)
				if (sz < len(parser.unworked_data)) || (e != nil) {
					// Были записаны не все данные
					panic(errors.New("Ошибка записи тела запроса")) // Возможно, стоит убрать
				}

				parser.unworked_data = nil
			}
		}

		res = append(res, parser.unfinished_req)
		parser.unfinished_req = nil
	} // for len(parser.unworked_data) > 0

	return res
} // func (p *BigRequestsParser) Parse() []*HttpBigRequest
