package main

import (
	"fmt"
	"os"
	"strconv"
	"sync"
	"time"
)

/*
Как заставить работать gogland (по крайней мере, под линуксом):
1) запилил папку GoglandProjects (должна быть в переменной GOPATH gogland-а)
2) Создал в ней папку src
3) Добавил туда ссылку на папку с нужным проектом
Как я понял, проект должен быть в папке src (а папка src - в папке, путь к которой есть в GOPATH),
чтобы отладка работала
*/

func main() {
	var StepsNum int = 10000
	var ConnectionsNum int = 1000

	l := len(os.Args)
	if l < 3 {
		fmt.Println("Аргументы: хост сервера, порт сервера [, кол-во отправок по каждому соединению, кол-во соединений]")
		return
	}

	ip := os.Args[1]
	port, err := strconv.Atoi(os.Args[2])
	if err != nil {
		fmt.Println(err.Error())
		return
	}

	if l > 3 {
		i, e := strconv.Atoi(os.Args[3])
		if e == nil {
			StepsNum = i
		} // if e == nil

		if l > 4 {
			i, e = strconv.Atoi(os.Args[4])
			if e == nil {
				ConnectionsNum = i
			}
		}
	} // if l > 2

	var wg sync.WaitGroup

	t0 := time.Now()
	for n := 0; n < ConnectionsNum; n++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			test_connect(ip, int(port), StepsNum)
		}()

		/*if ( n % 200 ) == 0 {
			time.Sleep(10*time.Millisecond)
		}*/
	}

	wg.Wait()
	fmt.Printf("Прошло %d миллисекунд на %d соединений и %d отправок по каждому\n",
		int(time.Since(t0))/1000000, ConnectionsNum, StepsNum)
	return
}
