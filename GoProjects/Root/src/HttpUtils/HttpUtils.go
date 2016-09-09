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
	http_version = "HTTP/1.1"

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

// Структура разбора запросов в виде набора байт (потоконебезопасная)
type RequestsParser struct {
	// Необработанные данные
	unworked_data []byte

	// Незаконченный объект запроса (не заполнены параметры и/или незаполненное тело)
	unfinished_req *HttpRequest

	// Размер недостающих данных для формирования тела объекта
	left_data_sz uint64
}

// Добавление новых данных
func (parser *RequestsParser) Write(data []byte) (int, error) {
	parser.unworked_data = append(parser.unworked_data, data...)
	return len(data), nil
} // func (p *RequestsParser) Write(data []byte) (int, error)

// Формирование одного запроса из прочитанных данных
// Результат - сформированный запрос (если есть) и наличие неправильно сформированного запроса
func (parser *RequestsParser) parse_one() (*HttpRequest, bool) {
	if len(parser.unworked_data) == 0 {
		// Нечего обрабатывать
		return nil, false
	}

	endl := []byte(endline_str)
	if parser.unfinished_req == nil {
		// Незавершённого запроса нет - формируем новый

		req_identifier := []byte(http_version + endline_str) // Признак запроса (версия HTTP + символы конца строки)

		for (parser.unfinished_req == nil) && (len(parser.unworked_data) > 0) {
			p_req_id := bytes.Index(parser.unworked_data, req_identifier)
			p_endl := -1
			if p_req_id >= 0 {
				p_endl = bytes.LastIndex(parser.unworked_data[:p_req_id], endl)
			} else {
				p_endl = bytes.LastIndex(parser.unworked_data, endl)
			}

			if p_endl >= 0 {
				// Обнаружен переход на следующую строку перед признаком запроса - косяк
				parser.unworked_data = parser.unworked_data[p_endl+len(endline_str):]
				if len(parser.unworked_data) == 0 {
					parser.unworked_data = nil
				}

				return nil, true
			} else if p_req_id >= 0 {
				// Найден признак запроса
				p_req_id += len(req_identifier)
				params := strings.Split(string(parser.unworked_data[:p_req_id]), " ")
				parser.unworked_data = parser.unworked_data[p_req_id:]
				if len(parser.unworked_data) == 0 {
					parser.unworked_data = nil
				}

				if len(params) != 3 {
					// Неверный заголовок parser.unworked_data = parser.unworked_data[:p_endl+len(endl)]
					return nil, true
				} // if len(params) != 3

				parser.unfinished_req = &HttpRequest{Type: params[0], Host: params[1]}
			} else { // if p_endl >= 0 {...} else if p_req_id >= 0 {
				// Не нашли ни признака запроса, ни перехода на следующую строку
				break
			}
		} // for (parser.unfinished_req == nil) && (len(parser.unworked_data) > 0)

		if parser.unfinished_req == nil {
			return nil, false
		}
	} // if parser.unfinished_req == nil

	if parser.left_data_sz == 0 {
		// Параметры запроса ещё не сформированы
		pos := bytes.Index(parser.unworked_data, endl)
		for ; pos >= 0; pos = bytes.Index(parser.unworked_data, endl) {
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
				// Обработали один параметр
				parser.unfinished_req.HeaderParams = append(parser.unfinished_req.HeaderParams, param)
			} else {
				// Ошибка разбора параметра заголовка
				parser.unfinished_req = nil
				return nil, true
			}
		} // for ; pos >= 0; pos = bytes.Index(parser.unworked_data, endl)

		if pos < 0 {
			// Символ перехода на следующую строку не найден
			return nil, false
		}

		// Если попали сюда - параметры заголовка считаны
		var body_len int = -1
		for _, param := range parser.unfinished_req.HeaderParams {
			if param.Name == BodySizeParamName {
				// Обнаружили параметр длины тела
				var e error
				body_len, e = strconv.Atoi(param.Value)
				if e != nil {
					// Некорректное значение параметра длины
					parser.unfinished_req = nil
					return nil, true
				}

				break
			}
		}

		if body_len > 0 {
			// В параметрах содержится ненулевая длина тела
			parser.left_data_sz = uint64(body_len)
		} else {
			// В параметрах не содержится длина тела - тупо записываем в него всё, что успели прочитать
			parser.left_data_sz = uint64(len(parser.unworked_data))
		}

		// Создаём пустой массив нужного размера (чтобы потом не выделять память повторно)
		parser.unfinished_req.Body = make([]byte, 0, parser.left_data_sz)
	} // if parser.left_data_sz == 0

	// Формируем тело запроса
	var cut_len uint64 = uint64(len(parser.unworked_data))
	if cut_len > parser.left_data_sz {
		cut_len = parser.left_data_sz
	}

	// Добавляем полученные данные в конец тела запроса
	parser.unfinished_req.Body = append(parser.unfinished_req.Body, parser.unworked_data[:cut_len]...)

	parser.unworked_data = parser.unworked_data[cut_len:]
	if len(parser.unworked_data) == 0 {
		parser.unworked_data = nil
	}

	var res *HttpRequest
	parser.left_data_sz -= cut_len
	if parser.left_data_sz == 0 {
		// Тело запроса полностью сформировано, запрос готов
		res = parser.unfinished_req
		parser.unfinished_req = nil
	}

	return res, false
} // func (parser *HttpRequest) parse_one() (*HttpRequest, bool)

// Разбор прочитанных данных и формирование массива запросов HTTP
// Если в данных встречается участок неправильного формата - для него формируется нулевой указатель
func (parser *RequestsParser) Parse() []*HttpRequest {
	var res []*HttpRequest

	for req, err := parser.parse_one(); (req == nil) == err; req, err = parser.parse_one() {
		res = append(res, req)
	}

	return res
} // func (parser *RequestsParser) Parse() []*HttpRequest

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
			var chan_sz uint8 = 2

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
