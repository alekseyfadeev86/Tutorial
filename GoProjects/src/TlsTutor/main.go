package main

import (
	"fmt"
	"encoding/pem"
	"net"
	"net/http"
	"os"
	"io/ioutil"
	"strconv"
)

/*
Как заставить работать goland (по крайней мере, под линуксом):
Папка с проектом ОБЯЗАТЕЛЬНО должна быть в папке src, которая находится в папке,
указанной в Global GOPATH проекта
*/

func conn_handler(conn net.Conn) {
	const html_data = `<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>Здравствуйте</title>
</head>
<body>
    <p><h1>Ура! Работает!</h1></p>
    <p>Пробная страница</p>
</body>
</html>`

	var resp string = "HTTP/1.1 200 OK\nContent-Length: " + strconv.Itoa(len(html_data)) +
		"\nContent-Type: text/html; charset=utf-8\n\n" + html_data

	if conn == nil {
		//fmt.Print(resp)
		return
	}

	defer conn.Close()

	buf := make([]byte, 1024)

	for {
		sz, e := conn.Read(buf)

		if e == nil {
			fmt.Printf("Получено %d байт: %s\n", sz, buf[:sz])
		} else {
			fmt.Printf("Ошибка чтения: %s\n", e.Error())
			break
		}

		resp_b := []byte(resp)
		for len(resp_b) > 0 {
			sz, e = conn.Write(resp_b)

			if e == nil {
				resp_b = resp_b[sz:]
			} else {
				fmt.Printf("Ошибка отправки: %s\n", e.Error())
				break
			}
		}

		fmt.Println("Ответ отправлен")
	}
} // func conn_handler(conn net.Conn)

type Handler struct {
	/*page              []byte
	favicon           []byte
	received_messages [][]byte
	mutex             sync.Mutex*/
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	const html_data = `<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>Здравствуйте</title>
</head>
<body>
    <p><h1>Ура! Работает!</h1></p>
    <p>Пробная страница</p>
</body>
</html>`

	if req.Method != "GET" {
		w.WriteHeader(http.StatusNotImplemented)
		return
	} else if req.URL.String() != "/" {
		w.WriteHeader(http.StatusNotFound)
		return
	}

	resp_body := []byte(html_data)
	w.WriteHeader(http.StatusOK)
	resp_header := w.Header()
	resp_header[http.CanonicalHeaderKey("Content-Type")] = []string{"text/html", "charset=utf-8"}
	resp_header[http.CanonicalHeaderKey("Content-Length")] = []string{strconv.Itoa(len(resp_body))}
	resp_header[http.CanonicalHeaderKey("Server")] = []string{"Golang test server"}

	for sz, e := w.Write(resp_body); (e == nil) && (len(resp_body) > 0); sz, e = w.Write(resp_body) {
		resp_body = resp_body[sz:]
	}
}

func main() {
	if false {
		cert, priv_key, err := create_cert_x509_bin(nil)

		if err != nil {
			fmt.Printf("Ошибка: %s\n", err.Error())
			return
		}

		fmt.Println("Успех")
		var pem_block pem.Block
		pem_block.Type = "CERTIFICATE"
		pem_block.Bytes = cert
		fmt.Printf("Сертификат:\n%s\n", string(pem.EncodeToMemory(&pem_block)))

		pem_block.Type = "PRIVATE KEY"
		pem_block.Bytes = priv_key
		fmt.Printf("Приватный ключ:\n%s\n", string(pem.EncodeToMemory(&pem_block)))
	}

	const fname_pref = "ca"
	const cert_fname string = fname_pref + ".pem"
	const priv_key_fname string = fname_pref + ".key"
	const http_cert_fname string = "cert_http.pem"
	const http_priv_key_fname string = "priv_key_http.pem"
	const host string = "127.0.0.1:44000" // "127.0.0.1:443"

	var gen_cert bool = false
	for _, arg := range os.Args[1:] {
		if arg == "gen_cert" {
			gen_cert = true
			break
		}
	}

	if gen_cert {
		// Удаляем старые файлы ключа и сертификата (если они есть)
		_ = os.Remove(cert_fname)
		_ = os.Remove(priv_key_fname)

		// func create_cert_x509_bin(cert_parent *x509.Certificate) (cert, priv_key []byte, err error)
		cert_b, priv_key_b, err := create_cert_x509_bin(nil)

		if err != nil {
			fmt.Printf("Ошибка создания сертификата и/или приватного ключа: %s\n", err.Error())
			return
		}

		// Сохраняем сертификат и приватный ключ в файл (каждый в свой)
		f_mode := os.ModeType | os.ModePerm
		err = ioutil.WriteFile(cert_fname, cert_b, f_mode)
		if err != nil {
			fmt.Printf("Ошибка записи сертификата в файл: %s\n", err.Error())
			return
		}

		err = ioutil.WriteFile(priv_key_fname, priv_key_b, f_mode)
		if err == nil {
			fmt.Printf("Сертификат и приватный ключ записаны в файлы %s и %s соответственно\n",
				cert_fname, priv_key_fname)
		} else {
			_ = os.Remove(cert_fname)
			fmt.Printf("Ошибка записи приватного ключа в файл: %s\n", err.Error())
		}

		_ = os.Remove(http_cert_fname)
		_ = os.Remove(http_priv_key_fname)

		var pem_block pem.Block
		cert_f, err := os.OpenFile(http_cert_fname, os.O_CREATE | os.O_WRONLY, f_mode)
		if err != nil {
			fmt.Printf("Ошибка открытия файла %s для записи: %s\n", http_cert_fname, err.Error())
			return
		}

		pem_block.Type = "CERTIFICATE"
		pem_block.Bytes = cert_b
		err = pem.Encode(cert_f, &pem_block)
		cert_f.Close()
		if err != nil {
			fmt.Printf("Ошибка записи файла PEM: %s\n", err.Error())
			_ = os.Remove(http_cert_fname)
			return
		}

		key_f, err := os.OpenFile(http_priv_key_fname, os.O_CREATE | os.O_WRONLY, f_mode)
		if err != nil {
			fmt.Printf("Ошибка открытия файла %s для записи: %s\n", http_priv_key_fname, err.Error())
			return
		}

		pem_block.Type = "PRIVATE KEY"
		pem_block.Bytes = priv_key_b
		err = pem.Encode(key_f, &pem_block)
		key_f.Close()
		if err != nil {
			fmt.Printf("Ошибка записи файла PEM: %s\n", err.Error())
			_ = os.Remove(http_priv_key_fname)
			return
		}
		fmt.Printf("Сертификат и приватный ключ в формате PEM записаны в файлы %s и %s соответственно\n",
			http_cert_fname, http_priv_key_fname)

		return
	} // if gen_cert

	// Запускаем сервер HTTP с использованием
	var err error
	var cert_b []byte
	var priv_key_b []byte

	// Считываем сертификат и приватный ключ
	cert_b, err = ioutil.ReadFile(cert_fname)
	if err != nil {
		fmt.Printf("Ошибка считывания сертификата: %s\n", err.Error())
		return
	}

	priv_key_b, err = ioutil.ReadFile(priv_key_fname)
	if err != nil {
		fmt.Printf("Ошибка считывания приватного ключа: %s\n", err.Error())
		return
	}

	// Запускаем сервер HTTP с использованием TLS
	fmt.Printf("Запускаем сервер на %s\n", host)
	err = http.ListenAndServeTLS(host, http_cert_fname, http_priv_key_fname, &Handler{})
	if err != nil {
		fmt.Printf("Ошибка запуска сервера: %s\n", err.Error())
	}
	//return

	//run_tls_srv(host, conn_handler)
	run_tls_srv(host, cert_b, priv_key_b, conn_handler)

	//var write_certs_to_files bool = true
	//create_cert_chk(write_certs_to_files)
}
