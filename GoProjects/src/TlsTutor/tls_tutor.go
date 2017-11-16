package main

import (
	"math/big"
	"crypto/x509"
	"crypto/x509/pkix"
	"crypto/rand"
	"crypto/rsa"
	"crypto/ecdsa"
	"crypto/tls"
	//"encoding/asn1"
	"encoding/pem"
	//"errors"
	"fmt"
	"time"
	"os"
	//"log"
	//"io/ioutil"
	"net"
)

func create_cert_x509_bin(cert_parent *x509.Certificate) (cert, priv_key []byte, err error) {
	//Формируем пару ключей RSA
	priv, e := rsa.GenerateKey(rand.Reader, 1024)
	if e != nil {
		err = e
		return
	}
	pub := &priv.PublicKey

	// Теперь формируем сертификат, подписанный parent-ом
	// (если parent - пустой, то формируем самоподписной сертификат)
	cert_temp := &x509.Certificate{}

	cert_temp.Version = 18

	// Формируем случайный серийник (!!! надо, т.к.
	// иначе при перегенерации сертификата браузеры будут ругаться !!!)
	cert_temp.SerialNumber = big.NewInt(int64(pub.E))
	cert_temp.SerialNumber.Mul(pub.N, big.NewInt(987654321))

	cert_temp.Subject = pkix.Name{
		Country: []string{"Somali"},
		Organization: []string{"Fake"},
		OrganizationalUnit: []string{"Fake"},
	}

	cert_temp.NotBefore = time.Date(2010, 1, 1, 0, 0, 0, 0, time.UTC)
	cert_temp.NotAfter = time.Date(2020, 1, 1, 0, 0, 0, 0, time.UTC)
	cert_temp.SubjectKeyId = []byte{1,2,3,4,5,6,7}
	cert_temp.BasicConstraintsValid = true
	cert_temp.KeyUsage = x509.KeyUsageDigitalSignature|x509.KeyUsageCertSign
	cert_temp.ExtKeyUsage = []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth, x509.ExtKeyUsageServerAuth}
	cert_temp.AuthorityKeyId = []byte{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}
	cert_temp.IsCA = false // true

	if cert_parent == nil {
		cert_parent = cert_temp
	}

	cert, err = x509.CreateCertificate(rand.Reader, cert_temp, cert_parent, pub, priv)
	if err != nil {
		return
	}

	priv_key = x509.MarshalPKCS1PrivateKey(priv)
	return
} // func create_cert_x509_bin(cert_parent *x509.Certificate) (cert, priv_key []byte, err error)

// Чтобы разобрать данные create_cert-а:
// x509.ParseCertificate(cert_bin []byte) []*x509.Certificate, error
// x509.ParsePKCS1PrivateKey(der []byte) (*rsa.PrivateKey, error)

func make_config(cert_b, priv_key_b []byte) (conf *tls.Config, err error) {
	/*cert_b, priv_key_b, err := create_cert_x509_bin(nil)
	if err != nil {
		fmt.Printf("Ошибка создания сертификата и/или ключа: %s\n", err.Error())
		return
	}*/

	cert, err := x509.ParseCertificate(cert_b)
	if err != nil {
		fmt.Printf("Ошибка создания сертификата: %s\n", err.Error())
		return
	}

	priv_key, err := x509.ParsePKCS1PrivateKey(priv_key_b)
	if err != nil {
		fmt.Printf("Ошибка создания ключа: %s\n", err.Error())
		return
	}

	pool := x509.NewCertPool()
	pool.AddCert(cert)

	srv_cert := tls.Certificate{Certificate: [][]byte{cert_b}, PrivateKey: priv_key}
	conf = &tls.Config{
		ClientAuth: tls.NoClientCert,
		Certificates: []tls.Certificate{srv_cert},
		ClientCAs: pool,
		Rand: rand.Reader,
	}

	return
}

func run_tls_srv(host string, cert_b, priv_key_b []byte, conn_handler func(conn net.Conn)) {
	if conn_handler == nil {
		panic("TODO: обработать conn_handler == nil")
	}

	//var err error
	/*cert_b, priv_key_b, err := create_cert_x509_bin(nil)
	if err != nil {
		fmt.Printf("Ошибка создания сертификата и/или ключа: %s\n", err.Error())
		return
	}*/

	/*cert, err := x509.ParseCertificate(cert_b)
	if err != nil {
		fmt.Printf("Ошибка создания сертификата: %s\n", err.Error())
		return
	}

	priv_key, err := x509.ParsePKCS1PrivateKey(priv_key_b)
	if err != nil {
		fmt.Printf("Ошибка создания ключа: %s\n", err.Error())
		return
	}

	pool := x509.NewCertPool()
	pool.AddCert(cert)

	srv_cert := tls.Certificate{Certificate: [][]byte{cert_b}, PrivateKey: priv_key}
	config := tls.Config{
		ClientAuth: tls.NoClientCert,
		Certificates: []tls.Certificate{srv_cert},
		ClientCAs: pool,
		Rand: rand.Reader,
	}
	conf_ptr := &config*/

	conf_ptr, err := make_config(cert_b, priv_key_b)
	if err != nil {
		return
	}

	acceptor, err := tls.Listen("tcp", host, conf_ptr)
	if err != nil {
		fmt.Printf("Ошибка запуска сервера: %s\n", err.Error())
		return
	}

	for {
		conn, err := acceptor.Accept()
		if err != nil {
			fmt.Printf("Ошибка приёма входящего соединения %s\n", err.Error())
			continue
		}

		//defer conn.Close()
		fmt.Println("Новое входящее соединение")
		go conn_handler(conn)
	}
} // func run_tls_srv(host string, cert_b, priv_key_b []byte, conn_handler func(conn net.Conn))

func pemBlockForKey(priv interface{}) *pem.Block {
	switch k := priv.(type) {
		case *rsa.PrivateKey:
			return &pem.Block{Type: "RSA PRIVATE KEY", Bytes: x509.MarshalPKCS1PrivateKey(k)}

		case *ecdsa.PrivateKey:
			b, err := x509.MarshalECPrivateKey(k)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Unable to marshal ECDSA private key: %v", err)
				os.Exit(2)
			}
			return &pem.Block{Type: "EC PRIVATE KEY", Bytes: b}

		default:
			return nil
	}
}

func create_cert_chk(write_certs_to_files bool){
	cert_temp := &x509.Certificate{}
	cert_temp.SerialNumber = big.NewInt(1234567890)
	cert_temp.NotBefore = time.Date(2010, 1, 1, 0, 0, 0, 0, time.UTC)
	cert_temp.NotAfter = time.Date(2020, 1, 1, 0, 0, 0, 0, time.UTC)
	cert_temp.KeyUsage = x509.KeyUsageKeyEncipherment | x509.KeyUsageDigitalSignature | x509.KeyUsageCertSign
	cert_temp.ExtKeyUsage = []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}
	cert_temp.BasicConstraintsValid = true

	subj_name := pkix.Name{}
	subj_name.Country = []string{"Sealand", "Сомали"}
	subj_name.Organization = []string{"Рога и копыта", "Шарашкина контора"}
	subj_name.OrganizationalUnit = []string{"Бухгалтерия", "Security"}
	subj_name.Locality = []string{"loc1", "loc2"}
	subj_name.Province = []string{"prov1", "prov2"}
	subj_name.StreetAddress = []string{"qaz", "qwe"}
	subj_name.PostalCode = []string{"12345", "abc"}
	subj_name.SerialNumber = "12345qwsa"
	subj_name.CommonName = "com_name"
	name1 := pkix.AttributeTypeAndValue{Type:[]int{1,2,3,4,5}, Value:interface{}("val1")}
	subj_name.Names = []pkix.AttributeTypeAndValue{name1}

	// Если вернуть 2 строки ниже - вылетит ошибка (хз, почему)
	//name2 := pkix.AttributeTypeAndValue{Type:[]int{5,6,7,8,9}, Value:interface{}("val2")}
	//subj_name.ExtraNames = []pkix.AttributeTypeAndValue{name2}

	//subj_name = pkix.Name{ Organization: []string{"Acme Co"},}
	cert_temp.Subject = subj_name

	cert_temp.AuthorityKeyId = []byte{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}
	cert_temp.MaxPathLen = 1024
	cert_temp.MaxPathLenZero = false
	cert_temp.DNSNames = []string{"name1", "name2", "name3"}
	cert_temp.ExcludedDNSDomains = []string{"ex_name1", "ex_name2", "ex_name3"}
	cert_temp.IsCA = false
	cert_temp.PermittedDNSDomains = []string{"perm1", "perm2"}
	cert_temp.PermittedDNSDomainsCritical = true
	cert_temp.SignatureAlgorithm = x509.MD5WithRSA
	cert_temp.SubjectKeyId = []byte{1,2,3,4,5,6,7}
	cert_temp.UnknownExtKeyUsage = nil

	// TODO: Для создания ключей использовать crypto/rsa, т.к. cert_temp.SignatureAlgorithm = x509.MD2WithRSA
	// (в иных случаях могут быть использованы, например, crypto/ecdsa)

	var bits_num int = 1024 // Соответствует длине хэша MD2
	priv_key_ptr, err := rsa.GenerateKey(rand.Reader, bits_num)

	if err != nil {
		fmt.Printf("Ошибка создания ключей RSA: %s\n", err.Error())
		return
	}

	cert, err := x509.CreateCertificate(rand.Reader, cert_temp, cert_temp, &priv_key_ptr.PublicKey, priv_key_ptr)
	if err == nil {
		fmt.Println("Успех")
	} else {
		fmt.Printf("Ошибка создания сертификата: %s\n", err.Error())
	}

	if !write_certs_to_files {
		return
	}

	cert_fname := "cert.pem"
	key_fname := "key.pem"

	// Удаляем старые сертификат и ключ, если такие имеются
	err = os.Remove(cert_fname)
	err = os.Remove(key_fname)

	cert_f, err := os.Create(cert_fname)
	defer cert_f.Close()

	if err != nil {
		fmt.Printf("ошибка открытия файла %s для записи: %s", cert_fname, err)
		return
	}

	pem.Encode(cert_f, &pem.Block{Type: "CERTIFICATE", Bytes: cert})
	fmt.Printf("Сертификат записан в файл %s\n", cert_fname)

	key_f, err := os.Create(key_fname) // os.OpenFile(key_fname, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0600)

	if err != nil {
		fmt.Printf("ошибка открытия файла %s для записи: %s", key_fname, err)
		return
	}

	defer key_f.Close()

	pem.Encode(key_f, pemBlockForKey(priv_key_ptr))
	fmt.Printf("Ключ записан в файл %s\n", key_fname)
	fmt.Println("Успех")
}

func tls_srv(host string) {
	var srv_name string = host

	var cert tls.Certificate
	panic("TODO: сформировать сертификат (!!! не путать tls.Certificate с x509.Certificate !!!)")

	conf := &tls.Config{}
	conf.Rand = rand.Reader
	conf.Time = nil
	conf.Certificates = []tls.Certificate{cert}
	//conf.NameToCertificate = nil
	//conf.GetCertificate = nil
	//conf.GetClientCertificate = nil
	//conf.GetConfigForClient = nil
	//conf.VerifyPeerCertificate = nil
	//conf.RootCAs = nil
	//conf.NextProtos = nil
	conf.ServerName = srv_name

	acceptor, err := tls.Listen("tcp", host, conf)

	if err != nil {
		fmt.Printf("Ошибка запуска сервера: %s\n", err.Error())
		return
	}

	defer acceptor.Close()

	panic("TODO: запилить работу сервера")
} // func tls_srv(host string, port uint16)
