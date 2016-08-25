package main

import (
	"fmt"
	"io"
	// "net"
	"net/http"
	"os"
	"strconv"
)

type Handler struct {
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, req *http.Request) {
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

	fmt.Println(req.Method, req.URL.String())

	if req.Method != "GET" {
		w.WriteHeader(http.StatusNotImplemented)
		return
	}

	url := req.URL.String()
	if url == "/" {
		w.Header()[http.CanonicalHeaderKey("Location")] = []string{"/index.html"}
		w.WriteHeader(http.StatusMovedPermanently)
		return
	}

	f, e := os.Open("." + url)
	if e != nil {
		w.WriteHeader(http.StatusNotFound)
		return
	}

	defer f.Close()
	buf := make([]byte, 100)
	var resp_body []byte
	for {
		sz, e := f.Read(buf)
		if (e != nil) && (e != io.EOF) {
			w.WriteHeader(http.StatusInternalServerError)
			fmt.Println(e.Error())
			return
		} else if sz > 0 {
			resp_body = append(resp_body, buf[:sz]...)

			if e != nil {
				break
			}
		} else {
			break
		}
	} // for

	resp_header := w.Header()
	if len(resp_body) > 0 {
		if url == "/favicon.ico" {
			resp_header[http.CanonicalHeaderKey("Content-Type")] = []string{"image/x-icon"}
		} else {
			resp_header[http.CanonicalHeaderKey("Content-Type")] = []string{http.DetectContentType(resp_body)}
		}

		resp_header[http.CanonicalHeaderKey("Content-Length")] = []string{strconv.Itoa(len(resp_body))}
	}

	w.WriteHeader(http.StatusOK)
	fmt.Println(string(resp_body))
	for len(resp_body) > 0 {
		sz, e := w.Write(resp_body)
		if e != nil {
			fmt.Println(e.Error())
			return
		}

		resp_body = resp_body[sz:]
	} // for len(resp_body) > 0 {
} // func (h *Handler) ServeHTTP(w http.ResponseWriter, req *http.Request) {

func main() {
	h := Handler{}
	err := http.ListenAndServe("127.0.0.1:8000", &h)
	if err != nil {
		fmt.Println(err.Error())
	}
}
