/*
AsioInteractiveBrokers (c) by Michiel van Slobbe

AsioInteractiveBrokers is licensed under a
Creative Commons Attribution-ShareAlike 3.0 Unported License.

You should have received a copy of the license along with this
work.  If not, see <http://creativecommons.org/licenses/by-sa/3.0/>.
*/

#include "AsioClientSocket.hpp"

#include "TwsSocketClientErrors.h"
#include "EWrapper.h"

#include <string.h>
#include <assert.h>

AsioClientSocket::AsioClientSocket ( boost::asio::io_service & service,
                                     EWrapper *ptr ) :
    EClientSocketBase ( ptr ),
    m_service ( service ),
    m_resolver ( m_service ),
    m_sending ( false )
{

}

AsioClientSocket::~AsioClientSocket()
{
    if ( m_socket.get() != 0 )
        m_socket->close();
}

bool AsioClientSocket::eConnect ( const char *host, unsigned int port, int clientId )
{
    boost::mutex::scoped_lock login_lock ( m_login_mutex );
    // reset errno
    errno = 0;
    if ( m_socket.get() != 0 && m_socket->is_open() )
    {
        errno = EISCONN;
        getWrapper()->error ( NO_VALID_ID, ALREADY_CONNECTED.code(), ALREADY_CONNECTED.msg() );
        return false;
    }
    m_socket.reset ( new boost::asio::ip::tcp::socket ( m_service ) );
    // use local machine if no host passed in
    if ( ! ( host && *host ) )
        host = "127.0.0.1";
    boost::asio::ip::tcp::resolver::query query ( boost::asio::ip::tcp::v4(),
            std::string ( host ),
            boost::lexical_cast<std::string> ( port ) );
    boost::system::error_code ec;
    boost::asio::ip::tcp::resolver::iterator iterator = m_resolver.resolve ( query, ec );
    if ( ec != 0 )
    {
        // return an error if we can't resolve the host
        getWrapper()->error ( NO_VALID_ID, CONNECT_FAIL.code(), CONNECT_FAIL.msg() );
        return false;
    }
    m_socket->connect ( *iterator );
    if ( !m_socket->is_open() )
    {
        // return an error if we can't connect to the host
        getWrapper()->error ( NO_VALID_ID, CONNECT_FAIL.code(), CONNECT_FAIL.msg() );
        return false;
    }

    setClientId ( clientId );
    onConnectBase();
    boost::asio::ip::tcp::no_delay option ( true );
    m_socket->set_option ( option );
    StartReadAsync();

    // block for five seconds or until we've succesfully connected
    bool success = m_login_condition.timed_wait ( login_lock, boost::posix_time::seconds ( 5 ) );
    return success && isConnected();
}

void AsioClientSocket::eDisconnect()
{
    if ( m_socket.get() != 0 )
        m_socket->close();
}

bool AsioClientSocket::isSocketOK() const
{
    return ( m_socket.get() != 0 && m_socket->is_open() );
}

void AsioClientSocket::StartSendAsync()
{
    assert ( m_socket.get() != 0 && m_socket->is_open() );
    if ( m_queued_outgoing_buffer.size() == 0 )
        m_sending = false;
    else
    {
        m_sending = true;
        size_t buffer_size ( m_queued_outgoing_buffer.size() > OUTGOING_BUFFER_SIZE ?
                             OUTGOING_BUFFER_SIZE :
                             m_queued_outgoing_buffer.size() );
        std::copy ( m_queued_outgoing_buffer.begin(), m_queued_outgoing_buffer.begin() + buffer_size, m_outgoing_buffer.begin() );
        m_socket->async_write_some ( boost::asio::buffer ( m_outgoing_buffer, buffer_size ), boost::bind ( &AsioClientSocket::AsyncSentSocket,
                                     this,
                                     boost::asio::placeholders::error,
                                     boost::asio::placeholders::bytes_transferred ) );
    }
}

void AsioClientSocket::StartReadAsync()
{
    assert ( m_socket.get() != 0 && m_socket->is_open() );
    m_socket->async_read_some ( boost::asio::buffer ( m_incoming_buffer ),
                                boost::bind ( &AsioClientSocket::AsyncReadSocket,
                                        this,
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::bytes_transferred )
                              );
}

void AsioClientSocket::AsyncSentSocket( boost::system::error_code const & error_code,
                                        size_t len )
{
    if ( error_code == 0 && len != 0 )
    {
        m_queued_outgoing_buffer.erase( m_queued_outgoing_buffer.begin(), m_queued_outgoing_buffer.begin() + len );
        if ( m_queued_outgoing_buffer.size() != 0 )
            StartSendAsync();
        else
            m_sending = false;
    }
    else
        getWrapper()->error ( NO_VALID_ID, SOCKET_EXCEPTION.code(), SOCKET_EXCEPTION.msg() );
}

void AsioClientSocket::AsyncReadSocket( boost::system::error_code const & error_code,
                                        size_t len )
{
    if ( error_code == 0 && len != 0 )
    {
        assert ( m_socket.get() != 0 && m_socket->is_open() );

        bool is_connected_before ( isConnected() );

        m_inBuffer.insert ( m_inBuffer.end(), m_incoming_buffer.begin(), m_incoming_buffer.begin() + len );
        assert ( m_inBuffer.size() < 80000 );
        checkMessages();

        // if processing this message now puts us in connected state, stop the timeout
        if ( !is_connected_before && isConnected() )
        {
            boost::mutex::scoped_lock login_lock ( m_login_mutex );
            m_login_condition.notify_all();
        }

        StartReadAsync();
    }
    else
    {
        getWrapper()->error ( NO_VALID_ID, SOCKET_EXCEPTION.code(), SOCKET_EXCEPTION.msg() );
    }
}

int AsioClientSocket::send(const char* buf, size_t sz)
{
    m_queued_outgoing_buffer.insert ( m_queued_outgoing_buffer.end(),
                                      buf,
                                      buf + sz );
    if ( !m_sending )
    {
        m_sending = true;
        StartSendAsync();
    }
    return sz;
}

int AsioClientSocket::receive(char* buf, size_t sz)
{
    // we just have to implement this for EClientSocketBase
    return 0;
}

bool AsioClientSocket::checkMessages()
{
    const char* beginPtr = &m_inBuffer[0];
    const char*     ptr = beginPtr;
    const char*     endPtr = ptr + m_inBuffer.size();
    try {
        while ( ( isConnected() ? processMsg ( ptr, endPtr )
                  : processConnectAck ( ptr, endPtr ) ) > 0 ) {
            if ( ( ptr - beginPtr ) >= ( int ) m_inBuffer.size() )
                break;
        }
    }
    catch ( ... ) {
        CleanupBuffer ( m_inBuffer, ( ptr - beginPtr ) );
        throw;
    }
    assert ( isConnected() );
    CleanupBuffer ( m_inBuffer, ( ptr - beginPtr ) );
    return true;
}

