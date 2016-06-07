# -*- coding: utf-8 -*-
__author__ = 'aleksey'

# В помощь:
# http://javascript.ru/window-location
# https://learn.javascript.ru/


import socket
import http_utils
import websocket_utils
import threading


def str_maker( bin_data ):
    try:
        return bin_data.decode( 'utf-8' )
    except:
        res = ''
        for b in bin_data:
            res += hex( b )
        return res


def runserver( host='127.0.0.1', port=8000, printer=None ):
    if not printer:
        print( 'Запускаем сервер на ' + host + ':' + str( port ) )
        printer = lambda l: None
    else:
        printer( 'Запускаем сервер на ' + host + ':' + str( port ) )

    srv_acceptor = socket.socket( socket.AF_INET, socket.SOCK_STREAM, socket.IPPROTO_TCP )
    srv_acceptor.bind( ( host, port ) )
    srv_acceptor.listen( 1 )

    def thread_func( sock, s_num ):
        class RespMaker:
            def __init__( self ):
                def ws_resp_maker( data ):
                    packs, left_data = websocket_utils.Package.parse_data( data )

                    if packs:
                        resp_msg = packs[ 0 ].data
                    else:
                        resp_msg = bytearray( 'hi'.encode( 'utf-8' ) )

                    # res = bytearray()
                    # for pkg in packs:
                    #     res += pkg.data.encode( 'utf-8' ) if type( pkg.data ) is str else pkg.data
                    #     res += websocket_utils.Package( fin=True,
                    #                                     opcode=1 if type( pkg.data ) is str else 2,
                    #                                     data=2*pkg.data ).serialize()
                    return websocket_utils.Package( fin=True,
                                                    opcode=1,
                                                    data=resp_msg ).serialize()
                self.__http_maker_ = http_utils.response_maker
                self.__ws_maker_ = ws_resp_maker
                self.__is_http_ = True

            def make_resp( self, data ):
                if self.__is_http_:
                    try:
                        p = data.find( '\n'.encode( 'utf-8' ) )
                        if p > 0:
                            p = data[ : p ].decode( 'utf-8' ).find( 'HTTP' )

                        work_as_http = p > 0
                    except UnicodeDecodeError:
                        work_as_http = False

                    if work_as_http or True:
                        res = self.__http_maker_( data )
                        if type( res ) is tuple:
                            assert len( res ) == 2
                            self.__is_http_ = False
                            return res[ 0 ].serialize()
                        else:
                            return res.serialize()

                return self.__ws_maker_( data )

            def need_to_close_if_no_data( self ):
                return self.__is_http_

        try:
            resp_maker = RespMaker()
            get_resp = resp_maker.make_resp
            conn.settimeout( 3.0 )
            go_on = True
            while go_on:
                req_data = bytearray()
                try:
                    while True:
                        new_data = conn.recv( 1024 )
                        if new_data:
                            req_data += new_data
                        else:
                            go_on = False
                            break
                except socket.timeout:
                    pass

                if not req_data:
                    if resp_maker.need_to_close_if_no_data():
                        break
                    printer( 'Туц...' + s_num )
                    continue

                req_str = str_maker( req_data )
                printer( 'Пришло ' + s_num + ':\n' + req_str + '\n\n\n' )
                resp_data = get_resp( req_data )
                assert resp_data
                resp_str = str_maker( resp_data )
                while resp_data:
                    sended_sz = sock.send( resp_data )
                    resp_data = resp_data[ sended_sz : ]
                printer( 'Ушло ' + s_num + ':\n' + resp_str + '\n\n\n' )
        except ConnectionError as exc:
            print( exc )
        except BaseException as exc:
            print( exc )
            raise exc

        sock.close()
        printer( 'Соединение закрыто ' + s_num )

    try:
        num = 0
        while True:
            conn, addr = srv_acceptor.accept()
            printer( 'Новое входящее соединение ' + str( num ) )
            new_thread = threading.Thread( target=thread_func, args=( conn, str( num ) ) )
            num += 1
            new_thread.daemon = True
            new_thread.start()
    except KeyboardInterrupt:
        print( 'Пока' )

if __name__ == "__main__":
    import sys
    h = '127.0.0.1'
    p = 8000
    v = False
    for i in range( 1, len( sys.argv ) - 1 ):
        if sys.argv[ i ] == '-h':
            h = sys.argv[ i + 1 ]
        elif sys.argv[ i ] == '-p':
            p = int( sys.argv[ i + 1 ] )
        elif sys.argv[ i ] == '-v':
            v = sys.argv[ i + 1 ] = '1'

    def logger( l ):
        print( l )
    runserver( host=h, port=p, printer=logger if v else None )
