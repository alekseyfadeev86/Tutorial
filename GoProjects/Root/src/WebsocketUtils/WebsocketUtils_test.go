package WebsocketUtils

import (
	"bytes"
	"sync"
	"testing"
)

func TestAcceptedKey(t *testing.T) {
	if GetAcceptKey("dGhlIHNhbXBsZSBub25jZQ==") != "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" {
		t.Error("Ошибка формирования ключа подтверждения")
	}
}

func use_mask(data []byte, mask [4]byte) {
	for t := range data {
		data[t] = data[t] ^ mask[t%4]
	}
}

func TestFrame(t *testing.T) {
	f := Frame{Fin: true, Type: 15, Mask: false, Data: []byte{1, 2, 3, 4, 5}}
	ser := f.Serialize()
	expected := append([]byte{0x8F, 5}, f.Data...)
	if bytes.Compare(expected, ser) != 0 {
		t.Fatalf("Ошибка Serialize: ожидали %v, получили %v\n", expected, ser)
	}

	f = Frame{Fin: false, Rsvs: [3]bool{true, false, true}, Type: 7, Mask: true, MaskKey: [4]byte{1, 2, 3, 4}, Data: []byte{1, 2, 3, 4, 5}}
	ser = f.Serialize()
	data := append([]byte{}, f.Data...)
	use_mask(data, f.MaskKey)
	expected = append([]byte{0x57, 0x85, 1, 2, 3, 4}, data...)
	if bytes.Compare(expected, ser) != 0 {
		t.Fatalf("Ошибка Serialize: ожидали %v, получили %v\n", expected, ser)
	}

	fdata := make([]byte, 126)
	for t := range fdata {
		fdata[t] = byte(t)
	}

	f = Frame{Fin: true, Type: 15, Mask: false, Data: fdata}
	ser = f.Serialize()
	expected = append([]byte{0x8F, 126, 0, 126}, f.Data...)
	if bytes.Compare(expected, ser) != 0 {
		t.Fatalf("Ошибка Serialize: ожидали %v, получили %v\n", expected, ser)
	}

	f = Frame{Fin: false, Rsvs: [3]bool{true, false, true}, Type: 7, Mask: true, MaskKey: [4]byte{1, 2, 3, 4}, Data: fdata}
	ser = f.Serialize()
	data = append([]byte{}, f.Data...)
	use_mask(data, f.MaskKey)
	expected = append([]byte{0x57, 0xFE, 0, 126, 1, 2, 3, 4}, data...)
	if bytes.Compare(expected, ser) != 0 {
		t.Fatalf("Ошибка Serialize: ожидали %v, получили %v\n", expected, ser)
	}

	fdata = make([]byte, 0x10101)
	for t := range fdata {
		fdata[t] = byte(t)
	}

	f = Frame{Fin: true, Type: 15, Mask: false, Data: fdata}
	ser = f.Serialize()
	expected = append([]byte{0x8F, 127, 0, 0, 0, 0, 0, 1, 1, 1}, f.Data...)
	if bytes.Compare(expected, ser) != 0 {
		t.Fatal("Ошибка Serialize")
	}

	f = Frame{Fin: false, Rsvs: [3]bool{true, false, true}, Type: 7, Mask: true, MaskKey: [4]byte{1, 2, 3, 4}, Data: fdata}
	ser = f.Serialize()
	data = append([]byte{}, f.Data...)
	use_mask(data, f.MaskKey)
	expected = append([]byte{0x57, 0xFF, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 3, 4}, data...)
	if bytes.Compare(expected, ser) != 0 {
		t.Fatal("Ошибка Serialize")
	}
} // func TestFrame(t *testing.T)

func TestFrameReader(t *testing.T) {
	frames := make([]Frame, 0, 8)
	frames = append(frames, Frame{Fin: true, Type: 15, Mask: false, Data: []byte{1, 2, 3, 4, 5}})
	frames = append(frames, Frame{Fin: false, Rsvs: [3]bool{true, false, true}, Type: 7, Mask: true, MaskKey: [4]byte{1, 2, 3, 4}, Data: []byte{1, 2, 3, 4, 5}})

	fdata := make([]byte, 126)
	for t := range fdata {
		fdata[t] = byte(t)
	}

	frames = append(frames, Frame{Fin: true, Type: 15, Mask: false, Data: fdata})
	frames = append(frames, Frame{Fin: false, Rsvs: [3]bool{true, false, true}, Type: 7, Mask: true, MaskKey: [4]byte{1, 2, 3, 4}, Data: fdata})

	fdata = make([]byte, 0x10101)
	for t := range fdata {
		fdata[t] = byte(t)
	}

	frames = append(frames, Frame{Fin: true, Type: 15, Mask: false, Data: fdata})
	frames = append(frames, Frame{Fin: false, Rsvs: [3]bool{true, false, true}, Type: 7, Mask: true, MaskKey: [4]byte{1, 2, 3, 4}, Data: fdata})

	data := make([]byte, 0, 0x20000)
	for _, f := range frames {
		data = append(data, f.Serialize()...)
	}

	res := make([]Frame, 0, 8)
	parser := FrameParser{}
	for len(data) > 0 {
		var d []byte
		if len(data) <= 3 {
			d = data
			data = nil
		} else {
			d = data[0:3]
			data = data[3:]
		}

		s, e := parser.Write(d)
		if (s < len(d)) || (e != nil) {
			t.Fatalf("Ошибка записи: %i, %s\n", s, e.Error())
		}

		res = append(res, parser.Parse()...)
	} // for len(data) > 0

	if len(frames) != len(res) {
		t.Fatalf("Ожидали %i пакетов, получили %i\n", len(frames), len(res))
	}

	for i := range frames {
		if bytes.Compare(frames[i].Data, res[i].Data) != 0 {
			t.Errorf("Ошибка в %i-м пакете: разные данные", i)
		}

		if frames[i].Fin != res[i].Fin {
			t.Errorf("Ошибка в %i-м пакете: разные Fin-ы", i)
		}

		if frames[i].Type != res[i].Type {
			t.Errorf("Ошибка в %i-м пакете: разные данные", i)
		}

		if frames[i].Mask != res[i].Mask {
			t.Errorf("Ошибка в %i-м пакете: разные признаки маски", i)
		}

		for j := 0; j < 3; j++ {
			if frames[i].Rsvs[j] != res[i].Rsvs[j] {
				t.Errorf("Ошибка в %i-м пакете: разные Rsvs", i)
				break
			}
		}

		for j := 0; j < 4; j++ {
			if frames[i].MaskKey[j] != res[i].MaskKey[j] {
				t.Errorf("Ошибка в %i-м пакете: разные ключи маски", i)
				break
			}
		}
	} // for i := range frames
} // func TestFrameReader(t *testing.T)

func TestBigFrame(t *testing.T) {
	fdata := []byte{1, 2, 3, 4, 5}
	ser_reader := Serialize(true, [3]bool{false, false, false}, 15, false, [4]byte{0, 0, 0, 0}, uint64(len(fdata)), bytes.NewReader(fdata))
	expected := append([]byte{0x8F, 5}, fdata...)

	ser := make([]byte, 0, len(fdata)+2)
	buf := make([]byte, 3)
	for sz, _ := ser_reader.Read(buf); sz > 0; sz, _ = ser_reader.Read(buf) {
		ser = append(ser, buf[:sz]...)
	}

	if bytes.Compare(expected, ser) != 0 {
		t.Fatalf("Ошибка Serialize: ожидали %v, получили %v\n", expected, ser)
	}

	fdata = []byte{1, 2, 3, 4, 5}
	fmask_key := [4]byte{1, 2, 3, 4}
	ser_reader = Serialize(false, [3]bool{true, false, true}, 7, true, fmask_key, uint64(len(fdata)), bytes.NewReader(fdata))
	data := append([]byte{}, fdata...)
	use_mask(data, fmask_key)
	expected = append([]byte{0x57, 0x85, 1, 2, 3, 4}, data...)
	ser = ser[:0]
	for sz, _ := ser_reader.Read(buf); sz > 0; sz, _ = ser_reader.Read(buf) {
		ser = append(ser, buf[:sz]...)
	}
	if bytes.Compare(expected, ser) != 0 {
		t.Fatalf("Ошибка Serialize: ожидали %v, получили %v\n", expected, ser)
	}

	fdata = make([]byte, 126)
	for t := range fdata {
		fdata[t] = byte(t)
	}

	ser_reader = Serialize(true, [3]bool{false, false, false}, 15, false, [4]byte{0, 0, 0, 0}, uint64(len(fdata)), bytes.NewReader(fdata))
	expected = append([]byte{0x8F, 126, 0, 126}, fdata...)
	ser = ser[:0]
	for sz, _ := ser_reader.Read(buf); sz > 0; sz, _ = ser_reader.Read(buf) {
		ser = append(ser, buf[:sz]...)
	}
	if bytes.Compare(expected, ser) != 0 {
		t.Fatalf("Ошибка Serialize: ожидали %v, получили %v\n", expected, ser)
	}

	ser_reader = Serialize(false, [3]bool{true, false, true}, 7, true, fmask_key, uint64(len(fdata)), bytes.NewReader(fdata))
	data = append([]byte{}, fdata...)
	use_mask(data, fmask_key)
	expected = append([]byte{0x57, 0xFE, 0, 126, 1, 2, 3, 4}, data...)
	ser = ser[:0]
	for sz, _ := ser_reader.Read(buf); sz > 0; sz, _ = ser_reader.Read(buf) {
		ser = append(ser, buf[:sz]...)
	}
	if bytes.Compare(expected, ser) != 0 {
		t.Fatalf("Ошибка Serialize: ожидали %v, получили %v\n", expected, ser)
	}

	fdata = make([]byte, 0x10101)
	for t := range fdata {
		fdata[t] = byte(t)
	}

	ser_reader = Serialize(true, [3]bool{false, false, false}, 15, false, [4]byte{0, 0, 0, 0}, uint64(len(fdata)), bytes.NewReader(fdata))
	expected = append([]byte{0x8F, 127, 0, 0, 0, 0, 0, 1, 1, 1}, fdata...)
	ser = ser[:0]
	for sz, _ := ser_reader.Read(buf); sz > 0; sz, _ = ser_reader.Read(buf) {
		ser = append(ser, buf[:sz]...)
	}
	if bytes.Compare(expected, ser) != 0 {
		t.Fatal("Ошибка Serialize")
	}

	ser_reader = Serialize(false, [3]bool{true, false, true}, 7, true, fmask_key, uint64(len(fdata)), bytes.NewReader(fdata))
	data = append([]byte{}, fdata...)
	use_mask(data, fmask_key)
	expected = append([]byte{0x57, 0xFF, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 3, 4}, data...)
	ser = ser[:0]
	for sz, _ := ser_reader.Read(buf); sz > 0; sz, _ = ser_reader.Read(buf) {
		ser = append(ser, buf[:sz]...)
	}
	if bytes.Compare(expected, ser) != 0 {
		t.Fatal("Ошибка Serialize")
	}
} // func TestBigFrame(t *testing.T)

func TestBigFrameParser(t *testing.T) {
	frames := make([]Frame, 0, 8)
	frames = append(frames, Frame{Fin: true, Type: 15, Mask: false, Data: []byte{1, 2, 3, 4, 5}})
	frames = append(frames, Frame{Fin: false, Rsvs: [3]bool{true, false, true}, Type: 7, Mask: true, MaskKey: [4]byte{1, 2, 3, 4}, Data: []byte{1, 2, 3, 4, 5}})

	fdata := make([]byte, 126)
	for t := range fdata {
		fdata[t] = byte(t)
	}

	frames = append(frames, Frame{Fin: true, Type: 15, Mask: false, Data: fdata})
	frames = append(frames, Frame{Fin: false, Rsvs: [3]bool{true, false, true}, Type: 7, Mask: true, MaskKey: [4]byte{1, 2, 3, 4}, Data: fdata})

	fdata = make([]byte, 0x10101)
	for t := range fdata {
		fdata[t] = byte(t)
	}

	frames = append(frames, Frame{Fin: true, Type: 15, Mask: false, Data: fdata})
	frames = append(frames, Frame{Fin: false, Rsvs: [3]bool{true, false, true}, Type: 7, Mask: true, MaskKey: [4]byte{1, 2, 3, 4}, Data: fdata})

	data := make([]byte, 0, 0x20000)
	for _, f := range frames {
		data = append(data, f.Serialize()...)
	}

	parser := BigFrameParser{}
	ch := make(chan BigFrame, 10)
	var wg sync.WaitGroup
	wg.Add(1)

	go func() {
		defer wg.Done()
		defer close(ch)

		for len(data) > 0 {
			var d []byte
			if len(data) <= 3 {
				d = data
				data = nil
			} else {
				d = data[0:3]
				data = data[3:]
			}

			s, e := parser.Write(d)
			if (s < len(d)) || (e != nil) {
				t.Fatalf("Ошибка записи: %i, %s\n", s, e.Error())
			}

			local_res := parser.Parse()
			for _, f := range local_res {
				ch <- f
			}
		} // for len(data) > 0
	}()

	res := make([]Frame, 0, 8)
	buf := make([]byte, 3)
	for n := 0; n < 8; n++ {
		f, chk := <-ch
		if !chk {
			break
		}

		new_f := Frame{Fin: f.Fin, Rsvs: f.Rsvs, Type: f.Type, Mask: f.Mask, MaskKey: f.MaskKey}
		for sz, _ := f.Data.Read(buf); sz > 0; sz, _ = f.Data.Read(buf) {
			new_f.Data = append(new_f.Data, buf[:sz]...)
		}

		res = append(res, new_f)
	}

	wg.Wait()

	if len(frames) != len(res) {
		t.Fatalf("Ожидали %i пакетов, получили %i\n", len(frames), len(res))
	} else if len(parser.unworked_data) != 0 {
		t.Errorf("Остались необработанные данные: %v\n", parser.unworked_data)
	}

	for i := range frames {
		if bytes.Compare(frames[i].Data, res[i].Data) != 0 {
			t.Errorf("Ошибка в %i-м пакете: разные данные", i)
		}

		if frames[i].Fin != res[i].Fin {
			t.Errorf("Ошибка в %i-м пакете: разные Fin-ы", i)
		}

		if frames[i].Type != res[i].Type {
			t.Errorf("Ошибка в %i-м пакете: разные типы", i)
		}

		if frames[i].Mask != res[i].Mask {
			t.Errorf("Ошибка в %i-м пакете: разные признаки маски", i)
		}

		for j := 0; j < 3; j++ {
			if frames[i].Rsvs[j] != res[i].Rsvs[j] {
				t.Errorf("Ошибка в %i-м пакете: разные Rsvs", i)
				break
			}
		}

		for j := 0; j < 4; j++ {
			if frames[i].MaskKey[j] != res[i].MaskKey[j] {
				t.Errorf("Ошибка в %i-м пакете: разные ключи маски", i)
				break
			}
		}
	} // for i := range frames

	test_frame := Frame{Fin: true, Rsvs: [3]bool{true, true, false}, Type: 10, Mask: true, MaskKey: [4]byte{5, 4, 2, 1}, Data: []byte{1, 2, 3, 4, 5, 6, 7, 8, 9, 0}}
	data = test_frame.Serialize()[:10]

	wg.Add(1)
	ch = make(chan BigFrame, 10)

	go func() {
		defer wg.Done()
		defer close(ch)

		for len(data) > 0 {
			var d []byte
			if len(data) <= 3 {
				d = data
				data = nil
			} else {
				d = data[0:3]
				data = data[3:]
			}

			s, e := parser.Write(d)
			if (s < len(d)) || (e != nil) {
				t.Fatalf("Ошибка записи: %i, %s\n", s, e.Error())
			}

			local_res := parser.Parse()
			for _, f := range local_res {
				ch <- f
			}
		} // for len(data) > 0

		e := parser.Close()
		if e != nil {
			t.Errorf("Ошибка закрытия parser-а: %s\n", e.Error())
		}
		e = parser.Close()
		if e != nil {
			t.Errorf("Ошибка повторного закрытия parser-а: %s\n", e.Error())
		}
	}()

	res = make([]Frame, 0, 8)
	for n := 0; n < 8; n++ {
		f, chk := <-ch
		if !chk {
			break
		}

		new_f := Frame{Fin: f.Fin, Rsvs: f.Rsvs, Type: f.Type, Mask: f.Mask, MaskKey: f.MaskKey}
		for sz, _ := f.Data.Read(buf); sz > 0; sz, _ = f.Data.Read(buf) {
			new_f.Data = append(new_f.Data, buf[:sz]...)
		}

		res = append(res, new_f)
	}

	wg.Wait()

	if len(res) != 1 {
		t.Fatalf("Ожидали 1 пакет, получили %i\n", len(res))
	}

	if bytes.Compare(test_frame.Data[:4], res[0].Data) != 0 {
		t.Error("Ошибка: неверные данные")
	}

	if test_frame.Fin != res[0].Fin {
		t.Error("Ошибка: разные Fin-ы")
	}

	if test_frame.Type != res[0].Type {
		t.Error("Ошибка: разные типы")
	}

	if test_frame.Mask != res[0].Mask {
		t.Error("Ошибка: разные признаки маски")
	}

	for j := 0; j < 3; j++ {
		if test_frame.Rsvs[j] != res[0].Rsvs[j] {
			t.Error("Ошибка: разные Rsvs")
			break
		}
	}

	for j := 0; j < 4; j++ {
		if test_frame.MaskKey[j] != res[0].MaskKey[j] {
			t.Error("Ошибка: разные ключи маски")
			break
		}
	}
} // func TestBigFrameParser(t *testing.T)
