package HttpUtils

import (
	"bytes"
	"strconv"
	"sync"
	"testing"
)

func TestCommon(t *testing.T) {
	hp := HeaderParam{Name: "param", Value: "val"}
	serialized := hp.Serialize()
	if serialized != "param: val" {
		t.Errorf("Ожидали: \"param: val\", получили: \"%s\"\n", serialized)
	}

	_, success := param_parser("qaz :qwerty")
	if success {
		t.Error("param_parser успешно разобрал заведомо неверную строку")
	}

	hp2, success := param_parser(serialized)
	if (hp.Value != hp2.Value) || (hp.Name != hp2.Name) || !success {
		t.Errorf("Неверный разбор параметра: было \"%s\", стало: \"%s: %s\"\n", serialized, hp2.Name, hp2.Value)
	}

	hparams := make([]HeaderParam, 2)
	hparams[0].Name = "param"
	hparams[0].Value = "value"
	hparams[1].Name = "параметр"
	hparams[1].Value = "значение"
	body := []byte("ПриветHello")

	req := HttpRequest{req_resp{hparams, body}, "GET", "/doc.html"}
	req_example := "GET /doc.html HTTP/1.1\r\nparam: value\r\nпараметр: значение\r\n\r\nПриветHello"
	if bytes.Compare([]byte(req_example), req.Serialize()) != 0 {
		t.Error("Ошибка метода Serialize запроса")
	}

	req2 := MakeRequest("GET", "/doc.html", hparams, body)
	check := true
	if (req2.Type != "GET") ||
		(req2.Host != "/doc.html") ||
		(bytes.Compare(req2.Body, body) != 0) ||
		(len(req2.HeaderParams) != len(hparams)) {
		check = false
	} else {
		for i, param := range req2.HeaderParams {
			if (param.Name != hparams[i].Name) || (param.Value != hparams[i].Value) {
				check = false
				break
			}
		}
	}

	if !check {
		t.Error("Ошибка в MakeRequest")
	}

	resp := HttpResponse{req_resp{hparams, body}, 200, "OK"}
	resp_example := "HTTP/1.1 200 OK\r\nparam: value\r\nпараметр: значение\r\n\r\nПриветHello"
	if bytes.Compare([]byte(resp_example), resp.Serialize()) != 0 {
		t.Error("Ошибка метода Serialize ответа")
	}

	resp2 := MakeResponse(200, "OK", hparams, body)
	check = true
	if (resp2.Code != 200) ||
		(resp2.What != "OK") ||
		(bytes.Compare(resp2.Body, body) != 0) ||
		(len(resp2.HeaderParams) != len(hparams)) {
		check = false
	} else {
		for i, param := range resp2.HeaderParams {
			if (param.Name != hparams[i].Name) || (param.Value != hparams[i].Value) {
				check = false
				break
			}
		}
	}

	if !check {
		t.Error("Ошибка в MakeResponse")
	}
} // func TestCommon(t *testing.T)

func compare_http_req(req1, req2 *HttpRequest) bool {
	if (req1 == nil) != (req2 == nil) {
		return false
	} else if req1 == nil {
		return true
	}

	if (req1.Type != req2.Type) ||
		(req1.Host != req2.Host) ||
		(len(req1.HeaderParams) != len(req2.HeaderParams)) ||
		(bytes.Compare(req1.Body, req2.Body) != 0) {
		return false
	} else if req1.HeaderParams != nil {
		for i, v := range req1.HeaderParams {
			if (req2.HeaderParams[i].Name != v.Name) || (req2.HeaderParams[i].Value != v.Value) {
				return false
			}
		}
	}

	return true
} // func compare_http_req( req1, req2 *HttpRequest )

func test_parser(parser *RequestsParser, src_data []byte, expected_res []*HttpRequest, expected_data_left []byte, t *testing.T) {
	var sz int
	var e error
	data_to_write := src_data

	uw_data_sz := len(parser.unworked_data)
	for len(data_to_write) > 0 {
		cur_data := data_to_write
		if len(cur_data) > 3 {
			cur_data = cur_data[:3]
		}

		cur_sz, cur_e := parser.Write(cur_data)
		if (cur_sz < len(cur_data)) && (cur_e == nil) {
			t.Fatal("Ошибка записи в парсер: ошибку не вернул, но записаны не все данные")
		}

		if (cur_sz > 0) && (cur_e == nil) {
			data_to_write = data_to_write[cur_sz:]
			sz += cur_sz
		} else {
			e = cur_e
			break
		}
	}

	if (sz != len(src_data)) || (e != nil) {
		t.Fatalf("Ошибка записи: ожидали %d, nil, получили %d, %s\n", len(src_data), sz, e.Error())
	} else if (len(parser.unworked_data) != (uw_data_sz + len(src_data))) ||
		(bytes.Compare(parser.unworked_data[uw_data_sz:], src_data) != 0) {
		t.Fatalf("Ошибка записи: данные записаны некорректно (записали %s, получили %s)\n", string(src_data), string(parser.unworked_data))
	}

	res := parser.Parse()
	if len(res) != len(expected_res) {
		t.Fatalf("Длина полученного массива (%d) не равна длине ожидаемого (%d)\n", len(res), len(expected_res))
	} else {
		for i := range expected_res {
			if expected_res[i] != nil {
				if res[i] == nil {
					t.Errorf("Ошибка в %d-м элементе результата: получен нулевой элемент, когда ожидался ненулевой\n", t)
				} else if !compare_http_req(res[i], expected_res[i]) {
					t.Errorf("Ошибка в %d-м элементе результата: ожидали %s, получили %s\n", t, res[i].Serialize(), expected_res[i].Serialize())
				}
			} else if res[i] != nil {
				t.Errorf("Ошибка в %d-м элементе результата: получен ненулевой элемент, когда ожидался нулевой\n", t)
			}
		}
	}

	if bytes.Compare(parser.unworked_data, expected_data_left) != 0 {
		t.Fatal("Массив необработанных данных не соответствует ожидаемому")
	}
} // func test_parser

func TestRequestsParser(t *testing.T) {
	var parser RequestsParser
	if (len(parser.unworked_data) != 0) || (parser.unfinished_req != nil) || (parser.left_data_sz != 0) {
		t.Fatal("Ошибка создания разборщика запросов")
	}

	garbage_data := []byte("\r\n\r\nqazqwerty\r\n")
	test_parser(&parser, garbage_data, []*HttpRequest{nil}, nil, t)

	req0 := MakeRequest("GET", "/", nil, nil)
	norm_data := req0.Serialize()
	req0.Body = garbage_data
	data := bytes.Join([][]byte{garbage_data, norm_data, garbage_data}, nil)
	test_parser(&parser, data, []*HttpRequest{nil, &req0}, nil, t)

	body := []byte("qazqwerty")
	head_params := []HeaderParam{{Name: BodySizeParamName, Value: strconv.Itoa(len(body))}}
	req0 = MakeRequest("GET", "/", head_params, body)
	norm_data = req0.Serialize()
	data = bytes.Join([][]byte{garbage_data, norm_data, garbage_data, norm_data, norm_data, garbage_data}, nil)
	test_parser(&parser, data, []*HttpRequest{nil, &req0, nil, &req0, &req0, nil}, nil, t)

	head_params = []HeaderParam{{Name: BodySizeParamName, Value: "qazqwerty"}}
	req0 = MakeRequest("GET", "/", head_params, body)
	norm_data = req0.Serialize()
	data = bytes.Join([][]byte{garbage_data, norm_data, garbage_data}, nil)
	test_parser(&parser, data, []*HttpRequest{nil, nil, nil}, nil, t)
} // func TestRequestsParser(t *testing.T)

func TestCommon2(t *testing.T) {
	buf := make([]byte, 3)
	serialized_data := []byte("qazqwertyHelloПривет")

	// Проверяем HttpBigRequest
	head_params := []HeaderParam{{Name: "p1_name", Value: "p1_val"}, {Name: "p2_name", Value: "p2_val"}}
	body := bytes.NewReader([]byte("HelloПривет"))
	req1 := HttpBigRequest{big_req_resp: big_req_resp{HeaderParams: head_params, Body: body}, Host: "/", Type: "GET"}
	req2 := MakeBigRequest("GET", "/", head_params, body)

	if (req1.Host != req2.Host) ||
		(req1.Type != req2.Type) ||
		(len(req1.HeaderParams) != len(req2.HeaderParams)) ||
		(req1.HeaderParams[0].Name != req2.HeaderParams[0].Name) ||
		(req1.HeaderParams[0].Value != req2.HeaderParams[0].Value) ||
		(req1.HeaderParams[1].Name != req2.HeaderParams[1].Name) ||
		(req1.HeaderParams[1].Value != req2.HeaderParams[1].Value) ||
		(req1.Body != req2.Body) {
		t.Fatal("Ошибка в MakeBigRequest")
	}

	serialized_req := SerializeBigRequest("GET", "/", head_params, body)
	if serialized_req == nil {
		t.Fatal("SerializeBigRequest вернул nil")
	}

	serialized_data = serialized_data[:0]
	for sz, _ := serialized_req.Read(buf); sz > 0; sz, _ = serialized_req.Read(buf) {
		serialized_data = append(serialized_data, buf[:sz]...)
	}

	fields := make([][]byte, 0, len(head_params)+3)
	fields = append(fields, []byte("GET / "+http_version))
	for _, p := range head_params {
		fields = append(fields, []byte(p.Serialize()))
	}
	fields = append(fields, []byte{})
	fields = append(fields, []byte("HelloПривет"))
	expected_data := bytes.Join(fields, []byte(endline_str))

	if bytes.Compare(serialized_data, expected_data) != 0 {
		t.Fatalf("Ошибка сериализации запроса: ожидали %s, получили %s\n", string(expected_data), string(serialized_data))
	}

	// Проверяем BigHttpResponse
	body = bytes.NewReader([]byte("HelloПривет"))
	resp1 := HttpBigResponse{big_req_resp: big_req_resp{HeaderParams: head_params, Body: body}, Code: 200, What: "Ok"}
	resp2 := MakeBigResponse(200, "Ok", head_params, body)

	if (resp1.Code != resp2.Code) ||
		(resp1.What != resp2.What) ||
		(len(resp1.HeaderParams) != len(resp2.HeaderParams)) ||
		(resp1.HeaderParams[0].Name != resp2.HeaderParams[0].Name) ||
		(resp1.HeaderParams[0].Value != resp2.HeaderParams[0].Value) ||
		(resp1.HeaderParams[1].Name != resp2.HeaderParams[1].Name) ||
		(resp1.HeaderParams[1].Value != resp2.HeaderParams[1].Value) ||
		(resp1.Body != resp2.Body) {
		t.Fatal("Ошибка в MakeBigResponse")
	}

	serialized_resp := SerializeBigResponse(200, "Ok", head_params, body)
	if serialized_resp == nil {
		t.Fatal("SerializeBigResponse вернул nil")
	}

	serialized_data = serialized_data[:0]
	for sz, _ := serialized_resp.Read(buf); sz > 0; sz, _ = serialized_resp.Read(buf) {
		serialized_data = append(serialized_data, buf[:sz]...)
	}

	fields = make([][]byte, 0, len(head_params)+3)
	fields = append(fields, []byte(http_version+" 200 Ok"))
	for _, p := range head_params {
		fields = append(fields, []byte(p.Serialize()))
	}
	fields = append(fields, []byte{})
	fields = append(fields, []byte("HelloПривет"))
	expected_data = bytes.Join(fields, []byte(endline_str))

	if bytes.Compare(serialized_data, expected_data) != 0 {
		t.Fatalf("Ошибка сериализации ответа: ожидали %s, получили %s\n", string(expected_data), string(serialized_data))
	}
} // func TestCommon2(t *testing.T)

func test_parser_big(parser *BigRequestsParser, src_data []byte, expected_res []*HttpRequest, expected_data_left []byte, t *testing.T) {
	var sz int
	var e error
	data_to_write := src_data

	for (len(data_to_write) > 0) && (e == nil) {
		cur_data := data_to_write
		if len(cur_data) > 3 {
			cur_data = cur_data[:3]
		}

		cur_sz, cur_e := parser.Write(cur_data)
		if (cur_sz < len(cur_data)) && (cur_e == nil) {
			t.Fatal("Ошибка записи в парсер: ошибку не вернул, но записаны не все данные")
		}

		if cur_sz > 0 {
			data_to_write = data_to_write[cur_sz:]
			sz += cur_sz
		}

		if cur_e != nil {
			e = cur_e
		}
	}

	if len(data_to_write) > 0 {
		t.Fatal("Были записаны не все данные")
	}

	unworked_big_reqs := parser.Parse()

	var parsed_reqs []*HttpRequest
	for ; len(unworked_big_reqs) > 0; unworked_big_reqs = unworked_big_reqs[1:] {
		if unworked_big_reqs[0] == nil {
			parsed_reqs = append(parsed_reqs, nil)
			continue
		}

		new_req := MakeRequest(unworked_big_reqs[0].Type, unworked_big_reqs[0].Host, unworked_big_reqs[0].HeaderParams, nil)
		buf := make([]byte, 10)
		for s_, e_ := unworked_big_reqs[0].Body.Read(buf); s_ > 0; s_, e_ = unworked_big_reqs[0].Body.Read(buf) {
			new_req.Body = append(new_req.Body, buf[:s_]...)
			if e_ != nil {
				break
			}
		}

		parsed_reqs = append(parsed_reqs, &new_req)
	} // for len(unworked_big_reqs) > 0

	if len(parsed_reqs) != len(expected_res) {
		t.Fatalf("Длина полученного массива (%d) не равна длине ожидаемого (%d)\n", len(parsed_reqs), len(expected_res))
	} else {
		for i := range expected_res {
			if expected_res[i] != nil {
				if parsed_reqs[i] == nil {
					t.Errorf("Ошибка в %d-м элементе результата: получен нулевой элемент, когда ожидался ненулевой\n", t)
				} else if !compare_http_req(parsed_reqs[i], expected_res[i]) {
					t.Errorf("Ошибка в %d-м элементе результата: ожидали %s, получили %s\n", t, parsed_reqs[i].Serialize(), expected_res[i].Serialize())
				}
			} else if parsed_reqs[i] != nil {
				t.Errorf("Ошибка в %d-м элементе результата: получен ненулевой элемент, когда ожидался нулевой\n", t)
			}
		}
	}

	if bytes.Compare(parser.unworked_data, expected_data_left) != 0 {
		t.Fatal("Массив необработанных данных не соответствует ожидаемому")
	}
} // func test_parser_big

func TestBigRequestsParser(t *testing.T) {
	var parser BigRequestsParser

	// Сформированные запросы
	if (len(parser.unworked_data) != 0) ||
		(parser.unfinished_req != nil) ||
		(parser.body_writer != nil) {
		t.Fatal("Ошибка создания разборщика запросов")
	}

	garbage_data := []byte("\r\n\r\nqazqwerty\r\n")
	test_parser_big(&parser, garbage_data, []*HttpRequest{nil}, nil, t)

	req0 := MakeRequest("GET", "/", nil, nil)
	norm_data := req0.Serialize()
	req0.Body = garbage_data
	data := bytes.Join([][]byte{garbage_data, norm_data, garbage_data}, nil)
	test_parser_big(&parser, data, []*HttpRequest{nil, &req0}, nil, t)

	body := []byte("qazqwerty")
	head_params := []HeaderParam{{Name: BodySizeParamName, Value: strconv.Itoa(len(body))}}
	req0 = MakeRequest("GET", "/", head_params, body)
	norm_data = req0.Serialize()
	data = bytes.Join([][]byte{garbage_data, norm_data, garbage_data, norm_data, norm_data, garbage_data}, nil)
	test_parser_big(&parser, data, []*HttpRequest{nil, &req0, nil, &req0, &req0, nil}, nil, t)

	head_params = []HeaderParam{{Name: BodySizeParamName, Value: "qazqwerty"}}
	req0 = MakeRequest("GET", "/", head_params, body)
	norm_data = req0.Serialize()
	data = bytes.Join([][]byte{garbage_data, norm_data, garbage_data}, nil)
	test_parser_big(&parser, data, []*HttpRequest{nil, nil, nil}, nil, t)

	{
		req_data := []byte("qazqwerty123")
		head_params = []HeaderParam{{Name: BodySizeParamName, Value: strconv.Itoa(len(req_data))}}
		req := MakeRequest("GET", "/", head_params, nil)

		parser.Write(req.Serialize())
		reqs := parser.Parse()
		if (len(reqs) != 1) || (reqs[0] == nil) {
			t.Fatal("Ошибка разбора запроса")
		}

		var wg sync.WaitGroup
		wg.Add(1)
		go func() {
			defer wg.Done()
			snd_buf := req_data
			for s_, e_ := parser.Write(snd_buf[:1]); (len(snd_buf) > 0) && (s_ > 0) && (e_ == nil); s_, e_ = parser.Write(snd_buf[:1]) {
				snd_buf = snd_buf[1:]
			}
		}()

		buf := make([]byte, len(req_data))
		parsed_body := make([]byte, 0, len(req_data))
		for s_, e_ := reqs[0].Body.Read(buf); e_ == nil; s_, e_ = reqs[0].Body.Read(buf) {
			parsed_body = append(parsed_body, buf[:s_]...)
		}
		if bytes.Compare(parsed_body, req_data) != 0 {
			t.Fatalf("Ошибка чтения тела запроса: ожидали %s, получили %s\n", string(req_data), string(parsed_body))
		}
		wg.Wait()

		if parser.body_writer != nil {
			t.Fatal("Ошибка: ненулевой писатель тела запроса после записи всех данных")
		} else if parser.unfinished_req != nil {
			t.Fatal("Ненулевой неоконченный запрос")
		}
	}

	{
		req_data := []byte("qazqwerty123")
		head_params = []HeaderParam{{Name: BodySizeParamName, Value: strconv.Itoa(len(req_data))}}
		req := MakeRequest("GET", "/", head_params, nil)

		parser.Write(req.Serialize())
		reqs := parser.Parse()
		if (len(reqs) != 1) || (reqs[0] == nil) {
			t.Fatal("Ошибка разбора запроса")
		}

		var cut_ln int = 6
		var wg sync.WaitGroup
		req_data = req_data[:cut_ln]
		wg.Add(1)
		go func() {
			defer wg.Done()
			snd_buf := req_data
			for len(snd_buf) > 0 {
				if s_, e_ := parser.Write(snd_buf[:1]); (s_ == 0) || (e_ != nil) {
					break
				}
				snd_buf = snd_buf[1:]
			}

			e := parser.Close()
			if e != nil {
				t.Errorf("Ошибка закрытия parser-а %s\n", e.Error())
			}
			e = parser.Close()
			if e != nil {
				t.Errorf("Ошибка повторного закрытия parser-а %s\n", e.Error())
			}
		}()

		buf := make([]byte, cut_ln)
		parsed_body := make([]byte, 0, len(req_data))
		for s_, e_ := reqs[0].Body.Read(buf); e_ == nil; s_, e_ = reqs[0].Body.Read(buf) {
			parsed_body = append(parsed_body, buf[:s_]...)
		}

		t.Log(len(parsed_body))

		if bytes.Compare(parsed_body, req_data) != 0 {
			t.Fatalf("Ошибка чтения тела запроса: ожидали %s, получили %s\n", string(req_data), string(parsed_body))
		}
		wg.Wait()
	}
} // func TestBigRequestsParser(t *testing.T)
