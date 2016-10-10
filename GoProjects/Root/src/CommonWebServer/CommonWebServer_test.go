package CommonWebServer

import (
	"HttpUtils"
	"WebsocketUtils"
	"bytes"
	"io"
	"net"
	"strconv"
	"strings"
	"sync"
	"testing"
)

func OneConnectionCheck(addr string, num uint64, t *testing.T) {
	conn, err := net.Dial("tcp", addr)
	if err != nil {
		t.Errorf("Ошибка подключения: %s\n", err.Error())
		return
	}
	defer conn.Close()

	req_body := make([]byte, 100)
	for t := range req_body {
		req_body[t] = byte((uint64(t) + num) % 0xFF)
	}

	head_params := []HttpUtils.HeaderParam{{Name: HttpUtils.BodySizeParamName, Value: strconv.Itoa(len(req_body))}}

	var unworked_data []byte
	buf := make([]byte, 1000)
	for n := uint64(0); n < 5; n++ {
		for t := range req_body {
			req_body[t] = byte((uint64(t)*n + num) % 0xFF)
		}

		// Шлём запрос
		req := HttpUtils.MakeRequest("GET", "/", head_params, req_body)
		data := req.Serialize()
		for sz, e := conn.Write(data); (sz > 0) && (len(data) > 0); sz, e = conn.Write(data) {
			if e != nil {
				t.Errorf("Ошибка записи %s\n", e.Error())
				return
			}
			data = data[sz:]
		}

		// Получаем и обрабатываем ответ
		for sz, e := conn.Read(buf); sz > 0; sz, e = conn.Read(buf) {
			if e != nil {
				t.Errorf("Ошибка чтения %s\n", e.Error())
				return
			}
			unworked_data = append(unworked_data, buf[:sz]...)
			resp, uw_data, succ := HttpUtils.ParseOneResp(unworked_data)
			unworked_data = uw_data

			if !succ {
				t.Fatal("Получили мусор в ответ на запрос")
			} else if resp != nil {
				if resp.Code != 200 {
					t.Errorf("Получили ответ с кодом %i, отличным от 200\n", resp.Code)
					return
				} else if resp.What != "OK" {
					t.Errorf("Получили ответ с описанием %s, отличным от OK\n", resp.What)
					return
				} else if bytes.Compare(req.Body, resp.Body) != 0 {
					t.Error("Тело ответа отличается от тела запроса")
					return
				} else if len(req.HeaderParams) != len(resp.HeaderParams) {
					t.Error("Параметры запроса отличаются от параметров ответа")
					return
				}

				for i, v := range resp.HeaderParams {
					if (req.HeaderParams[i].Name != v.Name) || (req.HeaderParams[i].Value != v.Value) {
						t.Error("Параметры запроса отличаются от параметров ответа")
						return
					}
				}

				break
			}
		} // for sz, e := conn.Read(buf); sz > 0; sz, e = conn.Read(buf)
	} // for n := 0; n < 100; n++

	if len(unworked_data) > 0 {
		t.Error("Остались необработанные данные")
	}
} // func OneConnectionCheck(addr string, num byte, t *testing.T)

func TestHttpServer(t *testing.T) {
	http_req_worker := func(req *HttpUtils.HttpRequest) (*HttpUtils.HttpResponse, WsWorker) {
		if req != nil {
			resp := HttpUtils.MakeResponse(200, "OK", req.HeaderParams, req.Body)
			return &resp, nil
		}

		resp := HttpUtils.MakeResponse(400, "Bad request", nil, nil)
		return &resp, nil
	}

	srv, err := RunNewServer("127.0.0.1", 45000, http_req_worker)

	if srv != nil {
		defer srv.Close()
		if err != nil {
			t.Fatalf("Ошибка запуска сервера %s, но возвращён ненулевой интерфейс\n", err.Error())
		}
	} else {
		if err != nil {
			t.Fatalf("Ошибка запуска сервера %s\n", err.Error())
		} else {
			t.Fatal("Ошибка запуска сервера: результат nil, nil")
		}
	}

	// for n := uint64(0); n < 1000; n++ {
	// 	OneConnectionCheck("127.0.0.1:45000", n, t)
	// }

	for num := 0; num < 10; num++ {
		var wg sync.WaitGroup
		for step := 0; step < 1000; step++ {
			wg.Add(1)
			N := uint64(step)
			go func() {
				defer wg.Done()
				OneConnectionCheck("127.0.0.1:45000", N, t)
			}()
		}

		wg.Wait()
	}
} // func TestHttpServer(t *testing.T)

func OneWebsockConnectionCheck(addr string, N uint64, t *testing.T) {
	conn, err := net.Dial("tcp", addr)
	if err != nil {
		t.Errorf("Ошибка подключения: %s\n", err.Error())
		return
	}
	defer conn.Close()

	// Переходим на протокол веб-сокетов
	ws_key := "Iv8io/9s+lYFgZWcXczP8Q=="
	head_params := []HttpUtils.HeaderParam{{"Upgrade", "websocket"}, {"Connection", "Upgrade"}, {"Sec-WebSocket-Key", ws_key}}
	req := HttpUtils.MakeRequest("GET", "/", head_params, nil)
	data := req.Serialize()
	var sz int
	var e error
	for sz, e = conn.Write(data); (sz > 0) && (len(data) > 0); sz, e = conn.Write(data) {
		data = data[sz:]
	}

	if len(data) > 0 {
		if e == nil {
			t.Fatal("Запрос отправлен не полностью, ошибка - nil")
		} else {
			t.Fatalf("Запрос отправлен не полностью, ошибка - %s\n", e.Error())
		}
	}

	// Ждём ответ
	buf := make([]byte, 100)
	var unworked_data []byte
	var resp *HttpUtils.HttpResponse
	for sz, _ := conn.Read(buf); sz > 0; sz, _ = conn.Read(buf) {
		unworked_data = append(unworked_data, buf[:sz]...)
		r, uw, succ := HttpUtils.ParseOneResp(unworked_data)
		resp = r
		unworked_data = uw

		if !succ || (resp != nil) {
			break
		}
	}

	if resp == nil {
		t.Fatal("Подтверждение не было получено")
	}

	if len(unworked_data) > 0 {
		t.Error("Остались необработанные данные")
	}

	// Проверяем ответ
	if resp.Code != 101 {
		t.Fatalf("Ошибка HTTP, код %d\n, ожидали 101", resp.Code)
	}

	var chks [3]bool
	for _, param := range resp.HeaderParams {
		if param.Name == "Upgrade" {
			if param.Value != "websocket" {
				t.Fatalf("Ошибка подтверждения: поле %s равно %s\n", param.Name, param.Value)
			}
			chks[0] = true
		} else if param.Name == "Connection" {
			if param.Value != "Upgrade" {
				t.Fatalf("Ошибка подтверждения: поле %s равно %s\n", param.Name, param.Value)
			}
			chks[1] = true
		} else if param.Name == "Sec-WebSocket-Accept" {
			expected_key := WebsocketUtils.GetAcceptKey(ws_key)
			if param.Value != expected_key {
				t.Fatalf("Ошибка подтверждения: поле %s равно %s вместо %s\n", param.Name, param.Value, expected_key)
			}
			chks[2] = true
		}
	} // for _, param := range resp.HeaderParams

	if !chks[0] {
		t.Fatal("Подтверждение не содержит поля Upgrade")
	} else if !chks[1] {
		t.Fatal("Подтверждение не содержит поля Connection")
	} else if !chks[2] {
		t.Fatal("Подтверждение не содержит поля Sec-WebSocket-Accept")
	}

	// Проверка отправки/получения пакетов
	var reader WebsocketUtils.FrameParser
	for num := 0; num < 100; num++ {
		v := (num % 2) == 0
		var mask_key [4]byte
		for j := 0; j < 4; j++ {
			mask_key[j] = byte((j*num + int(N)) % 0xFF)
		}

		f := WebsocketUtils.Frame{Fin: v, Rsvs: [3]bool{v, !v, v}, Type: byte(num) % 0xF, Mask: v, MaskKey: mask_key}
		f.Data = make([]byte, 0, 1000)
		for j := 0; j < 1000; j++ {
			f.Data = append(f.Data, byte((j*num+int(N))%0xFF))
		}

		f_ser := f.Serialize()
		for s, e := conn.Write(f_ser); (len(f_ser) > 0) && (s > 0); s, e = conn.Write(f_ser) {
			if e != nil {
				t.Fatalf("Ошибка отправки %s\n", e.Error())
			}

			f_ser = f_ser[s:]
		}

		if len(f_ser) > 0 {
			t.Fatal("Не весь пакет был отправлен")
		}

		var f_rcv *WebsocketUtils.Frame
		for s, _ := conn.Read(buf); s > 0; s, _ = conn.Read(buf) {
			reader.Write(buf[:s])
			if rcv_f := reader.Parse(); len(rcv_f) > 0 {
				// Получили пакет
				f_rcv = &(rcv_f[0])
				break
			} else if s < len(buf) {
				break
			}
		}

		// Сравниваем отправленный пакет и полученный
		if f_rcv == nil {
			t.Fatal("Ответный пакет не был получен")
		} else if f.Type != f_rcv.Type {
			t.Fatalf("Неверный тип полученного пакета: ожидали %d, получили %d\n", f.Type, f_rcv.Type)
		} else if bytes.Compare(f.Data, f_rcv.Data) != 0 {
			t.Fatal("Неверные данные в полученном пакете")
		}
	} // for num := 0; num < 100; num++
} // func OneWebsockConnectionCheck(addr string, num byte, t *testing.T)

type echo_ws_worker struct {
	sender func(f WebsocketUtils.Frame) error
}

func (w *echo_ws_worker) Initialize(sender func(f WebsocketUtils.Frame) error, closer io.Closer) {
	w.sender = sender
}

func (w *echo_ws_worker) Work(f WebsocketUtils.Frame) {
	w.sender(f)
}

func (w *echo_ws_worker) Close() error {
	return nil
}

func TestHttpWebsockServer(t *testing.T) {
	http_req_worker := func(req *HttpUtils.HttpRequest) (*HttpUtils.HttpResponse, WsWorker) {
		if req != nil {
			var ws_worker WsWorker
			var resp HttpUtils.HttpResponse

			hparams_map := make(map[string]string)
			for _, p := range req.HeaderParams {
				hparams_map[p.Name] = p.Value
			}

			check := false
			for _, p := range strings.Split(hparams_map["Upgrade"], ", ") {
				if p == "websocket" {
					check = true
					break
				}
			}

			if check {
				check = false
				for _, p := range strings.Split(hparams_map["Connection"], ", ") {
					if p == "Upgrade" {
						check = true
						break
					}
				}

				if check {
					key, check := hparams_map["Sec-WebSocket-Key"]
					if check {
						// Переходим на протокол веб-сокетов
						confirm_key := WebsocketUtils.GetAcceptKey(key)
						resp_hparams := make([]HttpUtils.HeaderParam, 3)
						resp_hparams[0] = HttpUtils.HeaderParam{Name: "Upgrade", Value: "websocket"}
						resp_hparams[1] = HttpUtils.HeaderParam{Name: "Connection", Value: "Upgrade"}
						resp_hparams[2] = HttpUtils.HeaderParam{Name: "Sec-WebSocket-Accept", Value: confirm_key}

						resp = HttpUtils.MakeResponse(101, "Switching protocols", resp_hparams, nil)
					}
				}
				ws_worker = &echo_ws_worker{}
			} else {
				t.Error("Некорректный запрос (ожидаем только запрос переключения на вебсокет)")
				resp = HttpUtils.MakeResponse(200, "OK", req.HeaderParams, req.Body)
			}

			return &resp, ws_worker
		} // if req != nil

		resp := HttpUtils.MakeResponse(400, "Bad request", nil, nil)
		return &resp, nil
	} // http_req_worker := func(req *HttpUtils.HttpRequest) (*HttpUtils.HttpResponse, WsWorker)

	srv, err := RunNewServer("127.0.0.1", 45000, http_req_worker)

	if srv != nil {
		defer srv.Close()
		if err != nil {
			t.Fatalf("Ошибка запуска сервера %s, но возвращён ненулевой интерфейс\n", err.Error())
		}
	} else {
		if err != nil {
			t.Fatalf("Ошибка запуска сервера %s\n", err.Error())
		} else {
			t.Fatal("Ошибка запуска сервера: результат nil, nil")
		}
	}

	for n := uint64(0); n < 100; n++ {
		OneWebsockConnectionCheck("127.0.0.1:45000", n, t)
	}

	for num := 0; num < 10; num++ {
		var wg sync.WaitGroup
		for step := 0; step < 1000; step++ {
			wg.Add(1)
			N := uint64(step)
			go func() {
				defer wg.Done()
				OneWebsockConnectionCheck("127.0.0.1:45000", N, t)
			}()
		}

		wg.Wait()
	}
} // func TestHttpWebsockServer(t *testing.T)
