/*
AsioInteractiveBrokers (c) by Michiel van Slobbe

AsioInteractiveBrokers is licensed under a
Creative Commons Attribution-ShareAlike 3.0 Unported License.

You should have received a copy of the license along with this
work.  If not, see <http://creativecommons.org/licenses/by-sa/3.0/>.
*/

#ifndef __ASIO_CLIENT_SOCKET_HPP
#define __ASIO_CLIENT_SOCKET_HPP

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/resolver_service.hpp>
#include <boost/asio/ip/basic_resolver.hpp>
#include <boost/bind.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/lexical_cast.hpp>

#include "EClientSocketBase.h"

#include <assert.h>
#include <time.h>

#define INCOMING_BUFFER_SIZE 8192
#define OUTGOING_BUFFER_SIZE 8192

class EWrapper;

class AsioClientSocket : public EClientSocketBase
{
public:

    explicit AsioClientSocket ( boost::asio::io_service & service,
                                EWrapper *ptr );
    ~AsioClientSocket();

    // override virtual funcs from EClient
    bool eConnect ( const char *host, unsigned int port, int clientId = 0 );
    void eDisconnect();

    bool isSocketOK() const;
    virtual bool checkMessages();
private:
    boost::asio::io_service & m_service;
    boost::shared_ptr<boost::asio::ip::tcp::socket> m_socket;
    boost::asio::ip::tcp::resolver m_resolver;

    boost::array<char, INCOMING_BUFFER_SIZE> m_incoming_buffer;
    boost::array<char, OUTGOING_BUFFER_SIZE> m_outgoing_buffer;
    std::vector<char> m_queued_outgoing_buffer;
    std::vector<char> m_pending_incoming_buffer;
    bool m_sending;

    // we use these to be able to time out if we're not logged in soon enough after connecting
    boost::mutex m_login_mutex;
    boost::condition m_login_condition;

    void StartSendAsync();
    void StartReadAsync();
    void AsyncReadSocket( boost::system::error_code const & error_code,
                          size_t len );
    void AsyncSentSocket( boost::system::error_code const & error_code,
                          size_t len );

    // part of EClientSocketBase
    int send(const char* buf, size_t sz);
    int receive(char* buf, size_t sz);
};

#endif

