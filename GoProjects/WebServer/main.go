package main

import (
	"fmt"
	"io"
	"os"
	"os/signal"
	"path"
	"strconv"
	"strings"
	"syscall"
)

type EchoWsWorker struct {
	reader FrameReader
}

func (w *EchoWsWorker) WorkData(buf []byte, sender func([]byte)) (bool, WorkerReplacer) {
	if sender == nil {
		return false, nil
	}

	w.reader.OnRead(buf)

	for f := w.reader.ParseOne(); f != nil; f = w.reader.ParseOne() {
		if f.Type == opcode_close {
			// Удалённая сторона хочет закрыть соединение - подтверждаем
			resp_f := Frame{Fin: true, Type: opcode_close, Data: f.Data}
			sender(resp_f.Serialize())
			return false, nil
		}

		resp_f := Frame{Fin: true, Type: opcode_text, Data: f.Data}
		sender(resp_f.Serialize())
	}

	return true, nil
}

func (w *EchoWsWorker) OnError(err error) bool {
	return err == nil
}

func make_handler(root_dir string) func(req *HttpRequest) (*HttpResponse, WorkerReplacer, bool) {
	return func(req *HttpRequest) (*HttpResponse, WorkerReplacer, bool) {
		if req == nil {
			return nil, nil, false
		}

		var resp *HttpResponse
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
					confirm_key := GetAcceptKey(key)
					resp_hparams := make([]Param, 3)
					resp_hparams[0] = Param{Name: "Upgrade", Value: "websocket"}
					resp_hparams[1] = Param{Name: "Connection", Value: "Upgrade"}
					resp_hparams[2] = Param{Name: "Sec-WebSocket-Accept", Value: confirm_key}

					resp = MakeHttpResponse(101, "Switching protocols", resp_hparams, nil)
					return resp, &EchoWsWorker{}, true
				}
			}
		}

		if req.Host == "/" {
			h_params := []Param{Param{Name: "Location", Value: "/index.html"}}
			resp = MakeHttpResponse(301, "Moved permanently", h_params, nil)
			return resp, nil, true
		}

		var ftype string = ""
		switch path.Ext(req.Host) {
		case "html":
			ftype = "text/html"
		case "txt":
			ftype = "text/plain"
		case "png":
			ftype = "image/png"
		case "ico":
			ftype = "image/x-icon"
		}

		f, e := os.Open(root_dir + req.Host)
		if e != nil {
			resp = MakeHttpResponse(404, "Нету!", nil, nil)
			return resp, nil, true
		} else {
			defer f.Close()
		}

		hexademical := "0123456789ABCDEF"
		switch req.Type {
		case "POST":
			s_body := string(req.Body)
			var req_body string
			for p := strings.Index(s_body, "%"); p >= 0; p = strings.Index(s_body, "%") {
				v := make([]byte, 1)
				v[0] = byte(strings.Index(hexademical, string(s_body[p+1]))) << 4
				v[0] |= byte(strings.Index(hexademical, string(s_body[p+2])))

				req_body = strings.Join([]string{req_body, s_body[:p], string(v)}, "")
				s_body = s_body[p+3:]
			}
			req_body = strings.Join([]string{req_body, s_body}, "")
			params := strings.Split(string(req_body), "&")
			resp_body := `<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>Введённые данные</title>
    <link rel="icon" href="favicon.ico" type="image/x-icon">
</head>
<body>
    <p><h1>Вы ввели:</h1></p>`
			for _, p := range params {
				resp_body += strings.Join([]string{"<p>", p, "</p>"}, "")
			}
			resp_body += `<p><a href="index.html">Перейти на главную страницу</a></p>
    <p><a href="form.html">Перейти на страницу с формой</a></p>
</body>
</html>`
			body := []byte(resp_body)
			header_params := make([]Param, 2)
			header_params[0] = Param{Name: body_size_param_name, Value: strconv.Itoa(len(body))}
			header_params[1] = Param{Name: "Content-Type", Value: "text/html"}
			resp = MakeHttpResponse(200, "OK", header_params, body)

		case "GET":
			body := make([]byte, 0, 100)
			buf := make([]byte, 100)
			for e == nil {
				sz, e := f.Read(buf)

				if sz == 0 {
					break
				} else if (e == nil) || (e == io.EOF) {
					body = append(body, buf[:sz]...)
				} else {
					resp = MakeHttpResponse(500, e.Error(), nil, nil)
				}
			}

			if (e != nil) && (e != io.EOF) {
				panic("Ошибка, которой быть не должно: " + e.Error())
			}
			header_params := make([]Param, 1, 2)
			header_params[0] = Param{Name: body_size_param_name, Value: strconv.Itoa(len(body))}
			if ftype != "" {
				header_params = append(header_params, Param{Name: "Content-Type", Value: ftype})
			}

			resp = MakeHttpResponse(200, "OK", header_params, body)
		default:
			resp = MakeHttpResponse(501, "Not supported", nil, nil)
		}

		return resp, nil, true
	}
} // func make_handler(root_dir string) func(req *HttpRequest) (*HttpResponse, WorkerReplacer, bool)

func main() {
	var host string = "127.0.0.1"
	var port uint16 = 45000
	var printer func(s string)

	ln := len(os.Args)
	if ln < 2 {
		fmt.Println("Аргументы: <Путь к корневой папке>[,<ip>][, <порт>][,<Вывод информации ввода-вывода>]")
		return
	} else if ln > 2 {
		host = os.Args[2]
		if ln > 3 {
			p, e := strconv.Atoi(os.Args[3])
			if e == nil {
				port = uint16(p)
			}

			if (ln > 4) && (os.Args[4] != "") {
				printer = func(s string) { fmt.Println(s) }
			}
		}
	}

	df, e := os.Open(os.Args[1])
	if e != nil {
		fmt.Println("Ошибка доступа к корневой директории: " + e.Error())
		return
	} else {
		df.Close()
	}

	handler := make_handler(path.Clean(os.Args[1]))
	dw_maker := WebWorkersFactoryMaker(handler)

	stopper := make(chan interface{}, 1)
	catcher := make(chan os.Signal, 1)
	signal.Notify(catcher, syscall.SIGINT)

	go func() {
		defer close(stopper)
		sig := <-catcher
		fmt.Println("Поймали ", sig, " останавливаем программу")
		stopper <- sig
	}()
	tcp_server(host, port, dw_maker, printer, stopper)
}
