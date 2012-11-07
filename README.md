USAGE

Just like EPosixClientSocket.cpp, AsioClientSocket.cpp inherits of EClientSocketBase. The purpose is to use our boost::asio auto completion to receive and process messages in one go. To this end, we still use the checkMessages() function and the code could be optimised further.

You would use this code when you've decided to use the excellent boost libraries together with the Interactive Brokers ( IB ) Trader Workstation ( TWS ) API.