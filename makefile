all :	
	g++ ./log/log.cpp\
		./http/http_conn.cpp\
		./CGImysql/connection_pool.cpp\
	    ./timer/timer.cpp\
		config.cpp\
		webserver.cpp\
		main.cpp\
		-o server -lpthread -lmysqlclient