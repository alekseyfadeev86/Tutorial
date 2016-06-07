# -*- coding: utf-8 -*-
__author__ = 'aleksey'

import websocket_utils

# Признаки окончания строки
endline = bytearray( '\r\n'.encode( 'utf-8' ) )

# Версия HTTP
http_version = 'HTTP/1.1'

# Разделитель имени параметра и его значения
param_splitter = ': '


class FormatError( RuntimeError ):
    """
    Ошибка формата запроса/ответа
    """
    def __init__( self, *args, **kwargs ):
        RuntimeError.__init__( self, *args, **kwargs )


class UnsupportableProtocol( RuntimeError ):
    """
    Протокол не поддерживается
    """
    def __init__( self, *args, **kwargs ):
        RuntimeError.__init__( self, *args, **kwargs )


def parse_req_resp_data( serialized_data ):
    """
    Разбирает строку запроса/ответа
    :param serialized_data: запрос, либо ответ HTTP
    :return: стартовая строка, список параметров заголовка (имён и значений), тело
    """

    def psplitter( l ):
        p = l.find( param_splitter )
        ln = len( param_splitter )
        return ( l[ : p ], l[ p + ln : ] ) if p >= 0 else ( l, '' )

    pos = serialized_data.find( endline )
    if pos < 0:
        # Строка не содержит символов перехода на следующую строку
        raise FormatError()

    body_splitter = 2*endline
    pos2 = serialized_data.find( body_splitter, pos )
    if pos2 < 0:
        # Строка не содержит разделителя между заголовком и телом (тело может быть пустым)
        raise FormatError()

    startline_str = serialized_data[ : pos ].decode( 'utf-8' )
    if pos == pos2:
        # Запрос не содержит параметров
        return startline_str, [], serialized_data[ pos2 + len( body_splitter ) : ]

    pos += len( endline )
    headers_data = [ psplitter( p.decode( 'utf-8' ) ) for p in serialized_data[ pos : pos2 ].split( endline ) if p ]
    return startline_str, headers_data, serialized_data[ pos2 + len( body_splitter ) : ]


class Request:
    """
    Класс запроса HTTP 1.1
    """
    def __init__( self,
                  req_type = None,
                  host = None,
                  header_params = [],
                  body = bytearray(),
                  serialized_data = None ):
        if serialized_data:
            # Разбираем строку с запросом
            startline, headers_data, body_data = parse_req_resp_data( serialized_data )
            startline_data = startline.split( ' ' )
            if len( startline_data ) != 3:
                # Неверное количество параметров в начальной линии
                raise FormatError()
            elif startline_data[ 2 ][ : len( http_version ) ] != http_version:
                raise UnsupportableProtocol()

            self.type = startline_data[ 0 ]
            self.host = startline_data[ 1 ]
            self.head_params = headers_data
            self.body = body_data
        else:
            # Формируем запрос по заданным параметрам
            if not ( type( req_type ) is str and
                     type( host ) is str and
                     type( header_params ) is list ):
                raise RuntimeError( 'Request type and host must be a string, and header_params must be a list of tuples!' )
            # TODO: проверить тип параметров header_params
            self.type = req_type
            self.host = host
            self.head_params = header_params
            self.body = bytearray( body )

    def serialize( self ):
        """
        Формирование набора байт запроса, готовых к отправке на сервер
        """
        endl = endline.decode( 'utf-8' )
        res = self.type + ' ' + self.host + ' ' + http_version + endl
        for pname, pval in self.head_params:
            if pname and pval:
                res += pname + param_splitter + pval + endl
        res += endl
        res = res.encode( 'utf-8' ) + self.body

        return res


class Response:
    """
    Класс ответа HTTP 1.1
    """
    def __init__( self,
                  resp_code = None,
                  resp_desc = '',
                  header_params = [],
                  body = bytearray(),
                  serialized_data = None ):
        if serialized_data is None:
            self.code = int( resp_code )
            self.what = str( resp_desc )
            self.head_params = header_params
            self.body = bytearray( body )
        else:
            # Разбираем строку с ответом
            startline, headers_data, body_data = parse_req_resp_data( serialized_data )
            startline_data = startline.split( ' ' )
            if len( startline_data ) < 2:
                # Неверное количество параметров в начальной линии
                raise FormatError()
            elif startline_data[ 0 ][ : len( http_version ) ] != http_version:
                raise UnsupportableProtocol()

            self.code = int( startline_data[ 1 ] )
            self.what = str( startline_data[ 2 ] ) if len( startline_data ) > 2 else ''
            self.head_params = headers_data
            self.body = body_data

    def serialize( self ):
        """
        Формирование набора байт запроса, готовых к отправке на сервер
        """
        endl = endline.decode( 'utf-8' )
        res = http_version + ' ' + str( self.code )
        if self.what:
            res += ' ' + self.what
        res += endl
        for pname, pval in self.head_params:
            if pname and pval:
                res += pname + param_splitter + pval + endl
        res += endl
        res = res.encode( 'utf-8' ) + self.body
        return res


def response_maker( req_data ):
    """
    Обрабатывает запрос и возвращает ответ
    :param req_str: строка запроса HTTP 1.1
    :return: объект ответа
    """

    # Формируем объект запроса
    try:
        req = Request( serialized_data=req_data )
    except UnsupportableProtocol:
        # Неподдерживаемая версия протокола HTTP
        return Response( resp_code=505, resp_desc='HTTP version is not supported' )
    except:
        # Ошибка разбора строки запроса
        return Response( resp_code=400, resp_desc='Bad request' )

    if req.type == 'GET':
        # Получен запрос с типом GET
        hparams = { p[ 0 ] : p[ 1 ] for p in req.head_params }
        if 'Upgrade' in hparams and 'websocket' in hparams[ 'Upgrade' ].split( ', ' ) and\
           'Connection' in hparams and 'Upgrade' in hparams[ 'Connection' ].split( ', ' ) and\
           'Sec-WebSocket-Key' in hparams:
            # Сервер хочет перейти на web-сокет
            key = hparams[ 'Sec-WebSocket-Key' ]
            confirm_key = websocket_utils.get_accept_key( key.encode( 'utf-8' ) ).decode( 'utf-8' )
            resp_params = [ ( 'Upgrade', 'websocket' ),
                            ( 'Connection', 'Upgrade' ),
                            ( 'Sec-WebSocket-Accept', confirm_key ) ]
            return ( Response( resp_code=101, resp_desc='Switching protocols', header_params=resp_params ), True )

        if not ( req.host and type( req.host ) is str and req.host[ 0 ] == '/' ):
            # Неверный хост
            return Response( resp_code=400, resp_desc='Bad request: incorrect host format' )

        if req.host == '/':
            return Response( resp_code=301,
                             resp_desc='Moved permanently',
                             header_params=[ ( 'Location', '/index.html' ) ] )
            full_filename = '.' + req.host + 'index.html'
        else:
            full_filename = '.' + req.host

        if full_filename[ -1 ] == '/':
            full_filename = full_filename[ : -1 ]

        pos = full_filename.rfind( '.' )
        file_type = full_filename[ pos + 1 : ] if pos > 0 else None
        if file_type == 'html':
            file_type = 'text/html'
        elif file_type == 'png':
            file_type = 'image/png'
        elif file_type == 'ico':
            file_type = 'image/x-icon'
        else:
            file_type = None

        # Ищем в текущей папке документ, соответствующий хосту запроса
        try:
            f = open( full_filename, 'rb' )
            resp_body = bytearray()
            for s in f:
                resp_body += s
            f.close()
        except FileNotFoundError:
            # Запрашиваемый документ не обнаружен
            return Response( resp_code=404, resp_desc='Not found' )

        # Формируем ответ
        # TODO: допилить
        params = []
        params.append( ( 'Server', 'Test' ) )
        # params.append( ( 'Content-Type', 'text/html; charset=utf-8' ) )
        if file_type:
            params.append( ( 'Content-Type', file_type ) )
        params.append( ( 'Content-Length', str( len( resp_body ) ) ) )
        return Response( resp_code=200, resp_desc='OK', header_params=params, body=resp_body )
    else:
        return Response( resp_code=501, resp_desc='Not implemented' )
