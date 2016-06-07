# -*- coding: utf-8 -*-
__author__ = 'aleksey'

import base64
import hashlib


def get_accept_key( req_key ):
    """
    Функция выполняет формирование ответного ключа для установки соединения по протоколу веб-сокетов
    :param req_key: ключ (в виде байт) из запроса на установку соединения
    :return: ответный ключ
    """
    postfix = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'.encode( 'utf-8' )
    return base64.b64encode( hashlib.sha1( req_key + postfix ).digest() )


class Package:
    """
    Класс пакета, передаваемого по протоколу веб-сокетов
    """
    def __init__( self,
                  fin = True,
                  rsv1 = False,
                  rsv2 = False,
                  rsv3 = False,
                  opcode = 0,
                  mask_key = None,
                  data = bytearray() ):
        opcode = int( opcode )
        if opcode < 0 or opcode > 0xF:
            raise ValueError( 'Opcode must be integer unsigned value 4-bits size' )

        if mask_key is not None:
            mask_key = int( mask_key )
            if mask_key < 0 or mask_key > 0xFFFFFFFF:
                raise ValueError( 'Mask key must be integer unsigned value 32-bits size' )

        self.__fin_  = bool( fin )
        self.__rsv1_ = bool( rsv1 )
        self.__rsv2_ = bool( rsv2 )
        self.__rsv3_ = bool( rsv3 )
        self.__opcode_ = opcode
        self.__mask_key_ = mask_key
        self.__data_ = bytearray( data.encode( 'utf-8' ) ) if type( data ) is str else bytearray( data )

    def __getattr__( self, item ):
        if item == 'fin':
            return self.__fin_
        elif item == 'rsv':
            return self.__rsv1_, self.__rsv2_, self.__rsv3_
        elif item == 'opcode':
            return self.__opcode_
        elif item in { 'mask', 'mask_key' }:
            return self.__mask_key_
        elif item == 'data':
            return self.__data_.decode( 'utf-8' ) if self.__opcode_ == 1 else self.__data_
        else:
            raise AttributeError( 'There is no attribute named ' + item )

    def serialize( self ):
        assert ( 0xFF & self.__opcode_ ) == self.__opcode_
        v = ( 0x80 if self.__fin_ else 0x00 ) | self.__opcode_
        if self.__rsv1_:
            v |= 0x40
        if self.__rsv2_:
            v |= 0x20
        if self.__rsv3_:
            v |= 0x10
        res = [ v ]

        v = 0x80 if self.__mask_key_ is not None else 0x00
        ln = len( self.__data_ )
        if ln < 126:
            v |= ln
            len_bytes = []
            # p = 2
        elif ln <= 0xFFFF:
            v |= 126
            len_bytes = [ 0xFF &( ln >> 8 ), 0xFF & ln ]
            # p = 4
        else:
            v |= 127
            len_bytes = [ 0xFF & ( ln >> 8*i ) for i in range( 7, -1, -1 ) ]
            # p = 10

        res.append( v )
        res.extend( len_bytes )

        if self.__mask_key_ is not None:
            mask_bytes = [ 0xFF & ( self.__mask_key_ >> 8*i ) for i in range( 3, -1, -1 ) ]
            res.extend( mask_bytes )
            # p += 4
            i = 0
            for v in self.__data_:
                res.append( mask_bytes[ i % 4 ] ^ v )
                i += 1  # i = 0 if i > 2 else ( i + 1 )
        else:
            res.extend( self.__data_ )

        return bytearray( res )

    @staticmethod
    def parse_data( serialized_data ):
        """
        Разбор данных, полученных по протоколу веб-сокетов
        :param serialized_data: исходные данные
        :return: набор пакетов, полученных из данных, и необработанный кусок исходных данных
        """
        def parse_one( data ):
            # "Откусывает" один пакет
            if len( data ) < 2:
                return None, data

            fin = ( 0x80 & data[ 0 ] ) != 0
            rsvs = ( ( ( 0x40 & data[ 0 ] ) != 0 ),
                     ( ( 0x20 & data[ 0 ] ) != 0 ),
                     ( ( 0x10 & data[ 0 ] ) != 0 ) )
            opcode = 0xF & data[ 0 ]
            mask_used = ( 0x80 & data[ 1 ] ) != 0
            len_val = 0x7F & data[ 1 ]
            if len_val < 126:
                ln = len_val
                len_val = 0
            elif len_val < 127:
                len_val = 2
            else:
                len_val = 8

            if len( data ) < ( len_val + 2 ):
                # Недостаточный размер данных
                return  None, data

            if len_val:
                ln = 0
                for val in data[ 2 : 2 + len_val ]:
                    ln = ( ln << 8 ) | val

            required_len = 2 + len_val + ln
            if mask_used:
                required_len += 4

            if len( data ) < required_len:
                return None, data

            p = 2 + len_val
            mask = [ 0, 0, 0, 0 ] if not mask_used else data[ p: p + 4 ]
            if mask_used:
                p += 4

            if mask_used:
                pack_data = []
                i = 0
                for v in data[ p : p + ln ]:
                    pack_data.append( mask[ i % 4 ] ^ v )
                    i += 1  # i = 0 if i > 2 else ( i + 1 )
            else:
                pack_data = data[ p : p + ln ]

            p += ln

            if mask_used:
                mask_key = 0
                for v in mask:
                    mask_key = ( mask_key << 8 ) | v
            else:
                mask_key = None
            pack = Package( fin=fin,
                            rsv1=rsvs[ 0 ],
                            rsv2=rsvs[ 1 ],
                            rsv3=rsvs[ 2 ],
                            opcode=opcode,
                            mask_key=mask_key,
                            data=pack_data )

            return pack, data[ p : ]

        packs, left_data = [], serialized_data

        while left_data:
            new_pack, left_data = parse_one( left_data )
            if new_pack:
                packs.append( new_pack )
            else:
                break

        return packs, left_data
