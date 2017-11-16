package main

/*
import (
	"bytes"
	"encoding/base64"
	"fmt"
	"io"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
)

const (
	fake_page = `<!DOCTYPE html>
<html lang="ru">
<head>
	<meta charset="utf-8">
	<title>Просто страница</title>
	<link rel="icon" href="favicon.ico" type="image/x-icon">
</head>
<body onload="alert(проверка)">
	<p>Просто страница-заплатка, которая в дальнейшем будет заменена на что-то другое</p>
</body>
</html>
`
)

type Handler struct {
	page              []byte
	favicon           []byte
	received_messages [][]byte
	mutex             sync.Mutex
}

func read_all(r io.Reader, expect_sz int) (data []byte, err error) {
	if r == nil {
		return
	}

	if expect_sz > 0 {
		data = make([]byte, 0, expect_sz)
	}

	buf := make([]byte, 1024)
	for sz, e := r.Read(buf); ; sz, e = r.Read(buf) {
		if (sz == 0) && (e == nil) {
			panic("io.Reader.Read вернул 0, nil")
		}

		if sz > 0 {
			data = append(data, buf[:sz]...)
		}

		if e != nil {
			if e != io.EOF {
				err = e
			}
			break
		}
	}

	return
} // func read_all(r io.Reader) (data []byte, err error)

func read_file(fname string, expect_sz int) (data []byte, err error) {
	if f, e := os.Open(fname); e == nil {
		defer f.Close()
		data, err = read_all(f, expect_sz)
	} else {
		err = e
	}

	return
} // func read_file(fname string, expect_sz int) (data []byte, err error)

func (h *Handler) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	url := req.URL.String()
	var resp_body []byte
	resp_header := w.Header()
	var content_type string

	if req.Method == "POST" {
		if url == "/msg" {
			// Поступило сообщение
			new_message, err := read_all(req.Body, int(req.ContentLength))
			if (err == nil) && (len(new_message) > 0) {
				h.mutex.Lock()
				defer h.mutex.Unlock()
				h.received_messages = append(h.received_messages, new_message)
			}
		} else if url == "/auth" {
			// Запрос авторизации
			auth, chk := req.Header["Authorization"]
			if !chk {
				resp_header[http.CanonicalHeaderKey("WWW-Authenticate")] = []string{"Basic"}
				w.WriteHeader(http.StatusUnauthorized)
				return
			}

			if bytes.Index([]byte(auth[0]), []byte("Basic ")) != 0 {
				w.WriteHeader(http.StatusBadRequest)
				return
			}

			b, e := base64.StdEncoding.DecodeString(auth[0][6:])
			if e != nil {
				w.WriteHeader(http.StatusBadRequest)
				return
			}

			login_pass := strings.Split(string(b), ":")
			if len(login_pass) != 2 {
				w.WriteHeader(http.StatusBadRequest)
				return
			}

			// Авторизация пройдена
			if login_pass[0] == "vasya" {
				resp_body = []byte("already_connected")
			} else if login_pass[0] == "petya" {
				resp_body = []byte("error")
			} else {
				resp_header[http.CanonicalHeaderKey("Set-Cookie")] = []string{"token=123456"}
				resp_body = []byte("/fake")
			}
		} else {
			w.WriteHeader(http.StatusNotFound)
			return
		}
	} else if req.Method == "GET" {
		if url == "/fake" {
			// Запросили фиктивную страницу - используется для отладки механизма авторизации
			req_header := req.Header
			if cookie, found := req_header[http.CanonicalHeaderKey("Cookie")]; found {
				// В запросе присутствую куки - проверяем, авторизован ли пользователь
				for _, p := range cookie {
					if pos := strings.Index(p, "token="); (pos != -1) && (p[pos+6:] == "123456") {
						// Авторизован - возвращаем фиктивную страницу
						resp_body = []byte(fake_page)
						break
					}
				}
			}

			if len(resp_body) == 0 {
				// Пользователь не авторизован - перекидываем на заглавную страницу
				resp_header[http.CanonicalHeaderKey("Location")] = []string{"/"}
				w.WriteHeader(http.StatusSeeOther)
				return
			}
		} else if url == "/favicon.ico" {
			// Запросили иконку
			resp_body = h.favicon
		} else if url == "/chat" {
			// Запросили страницу чата
			resp_body = h.page
		} else if url == "/data" {
			// Запросили полученные сообщения
			h.mutex.Lock()
			defer h.mutex.Unlock()
			resp_body = bytes.Join(h.received_messages, []byte("\n"))
			if len(resp_body) == 0 {
				resp_body = []byte("~~~~~")
			} else {
				resp_body = append(resp_body, []byte("\n")...)
			}
			h.received_messages = nil
		} else if url == "/" {
			// Запросили главную страницу
			req_header := req.Header

			// Проверяем, авторизовался ли пользователь
			if cookie, found := req_header[http.CanonicalHeaderKey("Cookie")]; found {
				// В запросе есть куки
				for _, p := range cookie {
					if pos := strings.Index(p, "token="); (pos != -1) && (p[pos+6:] == "123456") {
						// Пользователь авторизован - перекидываем его на фиктивную страницу
						// (позднее этот кусок будет заменён)
						resp_header[http.CanonicalHeaderKey("Location")] = []string{"/fake"}
						w.WriteHeader(http.StatusSeeOther)
						return
					}
				}
			}

			if len(resp_body) == 0 {
				// Пользователь не авторизован - перекидываем его на страницу авторизации
				var err error
				resp_body, err = read_file("./FrontEndPrototype/auth.html", 4*1024)
				if err != nil {
					// Косяк чтения файла
					w.WriteHeader(http.StatusInternalServerError)
					return
				}
			}

			// auth, chk := req.Header["Authorization"]
			// if chk {
			// 	fmt.Println(auth)
			// 	if bytes.Index([]byte(auth[0]), []byte("Basic ")) != 0 {
			// 		w.WriteHeader(http.StatusBadRequest)
			// 		return
			// 	}

			// 	b, e := base64.StdEncoding.DecodeString(auth[0][6:])
			// 	if e != nil {
			// 		fmt.Println(e)
			// 		w.WriteHeader(http.StatusBadRequest)
			// 		return
			// 	}

			// 	// Авторизация пройдена
			// 	f := "<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n<meta charset=\"UTF-8\">\n<title>Проверка</title>\n</head>\n<body>\n<h1>Здравствуй, %s</h1>\n</body>\n</html>"
			// 	resp_body = []byte(fmt.Sprintf(f, string(b)))
			// } else {
			// 	resp_header["WWW-Authenticate"] = []string{"Basic"}
			// 	w.WriteHeader(http.StatusUnauthorized)
			// 	return
			// }
		} else {
			w.WriteHeader(http.StatusNotFound)
			return
		}
	} else {
		w.WriteHeader(http.StatusNotImplemented)
		return
	}

	w.WriteHeader(http.StatusOK)
	if content_type == "" {
		content_type = http.DetectContentType(resp_body)
	}
	resp_header[http.CanonicalHeaderKey("Content-Type")] = []string{content_type}
	resp_header[http.CanonicalHeaderKey("Content-Length")] = []string{strconv.Itoa(len(resp_body))}
	resp_header[http.CanonicalHeaderKey("Server")] = []string{"Golang test server"}
	resp_header[http.CanonicalHeaderKey("qazqwerty")] = []string{"Golang test server"}

	for sz, e := w.Write(resp_body); (e == nil) && (len(resp_body) > 0); sz, e = w.Write(resp_body) {
		resp_body = resp_body[sz:]
	}

	// fmt.Println(req.Method)       // GET
	// fmt.Println(req.Proto)        // HTTP/1.1
	// fmt.Println(req.URL.String()) // /, /favicon.ico
	// fmt.Println(req.Header)       // map[string] []string
	// fmt.Println(req.Host)         // 127.0.0.1 или localhost или 192.168.0.102...
	// body := make([]byte, 0, 10000)
	// req.Body.Read(body)
	// fmt.Println(string(body))
	// w.WriteHeader(http.StatusNotFound)
	// return

	// fmt.Println(req.Method, req.URL.String())
} // func (h *Handler) ServeHTTP(w http.ResponseWriter, req *http.Request)

func main() {
	var chat_page []byte
	var favicon []byte
	var e error

	chat_page, e = read_file("./FrontEndPrototype/chat.html", 4*1024)
	if e != nil {
		fmt.Printf("Ошибка чтения страницы чата: %s\n", e.Error())
		return
	}

	favicon, e = read_file("./favicon.ico", -1)
	if e != nil {
		fmt.Printf("Ошибка чтения иконки: %s\n", e.Error())
		return
	}

	h := Handler{page: chat_page, favicon: favicon}
	host := "127.0.0.1"
	port := 8000
	l := len(os.Args)
	if l > 1 {
		host = os.Args[1]
		if l > 2 {
			if p, e := strconv.Atoi(os.Args[2]); e == nil {
				port = p
			}
		}
	}

	err := http.ListenAndServe(fmt.Sprintf("%s:%d", host, port), &h)
	if err != nil {
		fmt.Println(err.Error())
	}
}
*/
