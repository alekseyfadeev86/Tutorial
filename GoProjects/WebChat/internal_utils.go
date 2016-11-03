package main

import (
	"errors"
	"strconv"
	"sync"
)

var (
	// Ошибка: такого пользователя нет, либо неверный пароль
	ErrAuth = errors.New("Authorization error")

	// Ошибка: пользователь уже подключился
	ErrAlreadyConnect = errors.New("User already connected")

	// Ошибка: передан неизвестный ключ пользователя
	ErrUnknownToken = errors.New("Unknown user token")
)

// Структура с информацией о пользователе
type user_info struct {
	// Отображаемое в чате имя
	display_name string

	// Логин
	login string

	// Пароль
	password string

	// Используемый ключ
	token string
}

// Структура сообщения
type Message struct {
	// Имя автора
	Author string

	// Текст сообщеиня
	Text string
}

// Текуцщее состояние
type CurrentState struct {
	// Список кто в сети
	WhoOnline []string

	// Последние сообщения
	LastMessages []Message

	// № последнего сообщения
	LastMessageNumber uint64
}

// Структура чата
type Chat struct {
	// Список зарегистрированных пользователей (ключ - логин, значение - инфа о пользователе)
	registered_users map[string]*user_info

	// Список подключённых пользователей, отсортированных по ключам
	connected_users map[string]*user_info

	// Мьютекс для синхронизации доступа
	mutex sync.RWMutex

	// № последнего выданного ключа
	token_num int

	// № последнего сообщения (0 - сообщений не было)
	last_msg_num uint64

	// Буфер с сообщениями
	messages []Message
}

// Попытка подключения нового клиента (аргументы: имя пользователя и пароль,
// результат - ключ для дальнейшей идентификации этого пользователя и ошибка подключения)
func (c *Chat) Connect(login, password string) (token string, err error) {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	user, found := c.registered_users[login]
	if !found || (user.password != password) {
		// Такой пользователь не зарегистрирован, либо задан неверный пароль
		err = ErrAuth
	} else if user.token != "" {
		// Такой пользователь уже подключён
		err = ErrAlreadyConnect
	} else {
		user.token = strconv.Itoa(c.token_num)
		c.token_num++
		token = user.token
		c.connected_users[token] = user
	}

	return
} // func (c *chat) connect() (token string, err error)

// Сообщение об отключении клиента
func (c *Chat) Disconnect(token string) {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	if user, found := c.connected_users[token]; found {
		user.token = ""
		delete(c.connected_users, token)
	}
} // func (c *chat) disconnect(token string)

// Функция отправки сообщения в чат
func (c *Chat) Say(message, token string) error {
	if message == "" {
		// Пустое сообщение
		return nil
	}

	c.mutex.RLock()
	defer c.mutex.RUnlock()

	user, found := c.connected_users[token]
	if !found {
		// Отправитель неизвестен
		return ErrUnknownToken
	}

	new_message := Message{Author: user.display_name, Text: message}
	cp := uint64(cap(c.messages))
	if c.last_msg_num < cp {
		c.messages = append(c.messages, new_message)
	} else {
		c.messages[c.last_msg_num%cp] = new_message
	}

	c.last_msg_num++

	return nil
} // func (c *Chat) Say(message, token string) error

// Запрос текущего состояния и последних сообщений, начиная с messages_since
func (c *Chat) AskState(token string, messages_since uint64) (state CurrentState, err error) {
	c.mutex.RLock()
	defer c.mutex.RUnlock()

	_, found := c.connected_users[token]
	if !found {
		// Отправитель неизвестен
		err = ErrUnknownToken
		return
	}

	// Формруем список контактов в сети
	online_users := make([]string, 0, len(c.connected_users))
	for _, user := range c.connected_users {
		online_users = append(online_users, user.display_name)
	}

	if messages_since == 0 {
		messages_since = 1
	}

	if messages_since <= c.last_msg_num {
		cp := uint64(cap(c.messages))

		first := messages_since - 1
		if (first + cp) < c.last_msg_num {
			first = c.last_msg_num - cp
		}

		last := c.last_msg_num - 1

		first = first % cp
		last = last % cp

		if first < last {
			state.LastMessages = append([]Message{}, c.messages[first:last]...)
		} else {
			state.LastMessages = make([]Message, 0, cp)
			state.LastMessages = append(state.LastMessages, c.messages[first:cp]...)
			state.LastMessages = append(state.LastMessages, c.messages[0:last]...)
		}
	}
	state.LastMessageNumber = c.last_msg_num

	return
} // func (c *Chat) AskState(token string, messages_since uint64) (state CurrentState, err error)

// Зарегистрировать нового пользователя
func (c *Chat) RegisterUser(login, pass, disp_name string) bool {
	if (login == "") || (pass == "") || (disp_name == "") {
		return false
	}

	l := []rune(login)
	for _, c := range l {
		if (c < 0) || (c > rune(0x7F)) {
			return false
		}
	}

	l = []rune(pass)
	for _, c := range l {
		if (c < 0) || (c > rune(0x7F)) {
			return false
		}
	}

	c.mutex.Lock()
	defer c.mutex.Unlock()

	if _, found := c.registered_users[login]; found {
		return false
	}

	c.registered_users[login] = &user_info{login: login, password: pass, display_name: disp_name}
	return true
} // func (c *Chat) RegisterUser( login, pass, disp_name string ) bool

func (c *Chat) UnregisterUser(login string) {
	if login == "" {
		return
	}

	c.mutex.Lock()
	defer c.mutex.Unlock()

	if user, found := c.registered_users[login]; found {
		delete(c.registered_users, login)
		if user.token != "" {
			delete(c.connected_users, user.token)
		}
	}
} // func (c *Chat) UnregisterUser( login string )
