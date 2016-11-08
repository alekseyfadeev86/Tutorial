package main

import (
	"errors"
	"strconv"
	"sync"
	"sync/atomic"
	"time"
)

var (
	// Ошибка: такого пользователя нет, либо неверный пароль
	ErrAuth = errors.New("Authorization error")

	// Ошибка: пользователь уже подключился
	ErrAlreadyConnect = errors.New("User already connected")

	// Ошибка: передан неизвестный ключ пользователя
	ErrUnknownToken = errors.New("Unknown user token")

	// Ошибка: плохой аргумент
	ErrBadParam = errors.New("Bad argument")

	//Ошибка: пользователь уже зарегистрирован
	ErrUserAlreadyRegistered = errors.New("User already registered")
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

	// Время последней активности пользователя (в виде секунд с начала эпохи)
	last_activity_time int64
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

	// Список подключённых пользователей, отсортированный по ключам
	connected_users map[string]*user_info

	// Мьютекс для синхронизации доступа к буферам пользователей
	users_mutex sync.RWMutex

	// № последнего выданного ключа
	token_num int

	// № последнего сообщения (0 - сообщений не было)
	last_msg_num uint64

	// Буфер с сообщениями
	messages []Message

	messages_mutex sync.RWMutex
}

// Зарегистрировать нового пользователя
func (c *Chat) RegisterUser(login, pass, disp_name string) error {
	if (login == "") || (pass == "") || (disp_name == "") {
		return ErrBadParam
	}

	l := []rune(login)
	for _, c := range l {
		if (c < 0) || (c > rune(0x7F)) {
			return ErrBadParam
		}
	}

	l = []rune(pass)
	for _, c := range l {
		if (c < 0) || (c > rune(0x7F)) {
			return ErrBadParam
		}
	}

	c.users_mutex.Lock()
	defer c.users_mutex.Unlock()

	if _, found := c.registered_users[login]; found {
		// Такой пользователь уже зарегистрирован
		return ErrUserAlreadyRegistered
	}

	c.registered_users[login] = &user_info{login: login, password: pass, display_name: disp_name}
	return nil
} // func (c *Chat) RegisterUser( login, pass, disp_name string ) bool

func (c *Chat) UnregisterUser(login string) {
	if login == "" {
		return
	}

	c.users_mutex.Lock()
	defer c.users_mutex.Unlock()

	if user, found := c.registered_users[login]; found {
		delete(c.registered_users, login)
		if user.token != "" {
			delete(c.connected_users, user.token)
		}
	}
} // func (c *Chat) UnregisterUser( login string )

// Попытка подключения нового клиента (аргументы: имя пользователя и пароль,
// результат - ключ для дальнейшей идентификации этого пользователя и ошибка подключения)
func (c *Chat) Connect(login, password string) (token string, err error) {
	c.users_mutex.Lock()
	defer c.users_mutex.Unlock()

	user, found := c.registered_users[login]
	if !found || (user.password != password) {
		// Такой пользователь не зарегистрирован, либо задан неверный пароль
		err = ErrAuth
	} else if user.token != "" {
		// Такой пользователь уже подключён
		err = ErrAlreadyConnect
	} else {
		user.token = strconv.Itoa(c.token_num)
		user.last_activity_time = time.Now().Unix()
		c.token_num++
		token = user.token
		c.connected_users[token] = user
	}

	return
} // func (c *chat) connect() (token string, err error)

// Сообщение об отключении клиента
func (c *Chat) Disconnect(token string) {
	c.users_mutex.Lock()
	defer c.users_mutex.Unlock()

	if user, found := c.connected_users[token]; found {
		user.token = ""
		delete(c.connected_users, token)
	}
} // func (c *chat) disconnect(token string)

// "Пинг" пользователя (показывает, что он в сети)
// К моменту вызова должна быть захвачена блокировка users_mutex-а на чтение
// Если возвращает nil - пользователь был удалён по таймауту, либо его и не было
// (блокировка users_mutex-а отпущена в любом из этих случаев); если результат - не nil,
// то это указатель на найденного пользователя
func (c *Chat) internal_ping(token string) *user_info {
	locker := c.users_mutex.RLocker()
	defer func() {
		if locker != nil {
			locker.Unlock()
		}
	}()

	if user, found := c.connected_users[token]; found {
		now_t := time.Now().Unix()
		usr_t := atomic.LoadInt64(&user.last_activity_time)

		if now_t <= (usr_t + 3) {
			// Обновляем время последней активности
			locker = nil
			atomic.CompareAndSwapInt64(&user.last_activity_time, usr_t, now_t)
			if user == nil {
				panic("Internal error: user == nil")
			}
			return user
		} else {
			// Пингов давно не было - удаляем
			token = user.token

			c.users_mutex.RUnlock()
			c.users_mutex.Lock()
			locker = &c.users_mutex

			if user, found = c.connected_users[token]; found && (user.token == token) {
				// Пользователь не был передобавлен, пока перезахватывали блокировку
				user.token = ""
				delete(c.connected_users, token)
			}

			return nil
		}
	} else {
		return nil
	}
} // func (c *Chat) internal_ping(token string) bool

// "Пинг" пользователя (показывает, что он в сети)
func (c *Chat) Ping(token string) error {
	unlock_on_exit := true
	c.users_mutex.RLock()
	defer func() {
		if unlock_on_exit {
			c.users_mutex.RUnlock()
		}
	}()

	if c.internal_ping(token) == nil {
		unlock_on_exit = false
		return ErrUnknownToken
	}

	return nil
} // func (c *Chat) Ping(token string)

// Функция отправки сообщения в чат
func (c *Chat) Say(message, token string) error {
	if message == "" {
		// Пустое сообщение
		return nil
	}

	unlock_on_exit := true
	c.users_mutex.RLock()
	defer func() {
		if unlock_on_exit {
			c.users_mutex.RUnlock()
		}
	}()

	if user := c.internal_ping(token); user != nil {
		new_message := Message{Author: user.display_name, Text: message}

		c.messages_mutex.Lock()
		defer c.messages_mutex.Unlock()
		cp := uint64(cap(c.messages))
		last_msg_num := atomic.LoadUint64(&c.last_msg_num)
		if last_msg_num < cp {
			// В буфере сообщений ещё есть место для записи сообщений без перевыделения памяти
			c.messages = append(c.messages, new_message)
		} else {
			// В буфере сообщений нет места для записи новых сообщений - перезаписываем
			c.messages[last_msg_num%cp] = new_message
		}

		atomic.AddUint64(&c.last_msg_num, 1)
	} else {
		// Пользователь "просрочен" (давно не проявлял активности)
		unlock_on_exit = false
		return ErrUnknownToken
	}

	return nil
} // func (c *Chat) Say(message, token string) error

// Запрос текущего состояния и последних сообщений, начиная с messages_since
func (c *Chat) AskState(token string, messages_since uint64) (state CurrentState, err error) {
	unlock_on_exit := true
	c.users_mutex.RLock()
	defer func() {
		if unlock_on_exit {
			c.users_mutex.RUnlock()
		}
	}()

	if c.internal_ping(token) == nil {
		// Отправитель неизвестен, либо просрочен
		unlock_on_exit = false
		err = ErrUnknownToken
		return
	}

	// Формруем список контактов в сети
	online_users := make([]string, 0, len(c.connected_users))
	for _, user := range c.connected_users {
		online_users = append(online_users, user.display_name)
	}

	// Формируем список сообщений
	if messages_since == 0 {
		messages_since = 1
	}

	last_msg_num := atomic.LoadUint64(&c.last_msg_num)
	if messages_since <= last_msg_num {
		c.messages_mutex.RLock()
		c.messages_mutex.RUnlock()

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
	state.LastMessageNumber = last_msg_num

	return
} // func (c *Chat) AskState(token string, messages_since uint64) (state CurrentState, err error)
