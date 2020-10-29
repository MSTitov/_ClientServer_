#include "stdafx.h"
#include <fstream>
#include <signal.h>
#include <boost/date_time.hpp>
#include <boost/locale.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio/detail/socket_types.hpp>
#include <boost/asio/signal_set.hpp>

using namespace boost::asio;
using namespace boost::posix_time;
using namespace boost::local_time;
io_service service;

class talk_to_client;
typedef boost::shared_ptr<talk_to_client> client_ptr;
typedef std::vector<client_ptr> array;
array clients;

#define MEM_FN(x) boost::bind(&self_type::x, shared_from_this())
#define MEM_FN1(x,y) boost::bind(&self_type::x, shared_from_this(),y)
#define MEM_FN2(x,y,z) boost::bind(&self_type::x, shared_from_this(),y,z)

void update_clients_changed();

class talk_to_client : public boost::enable_shared_from_this<talk_to_client>, boost::noncopyable
{
	typedef talk_to_client self_type;
	talk_to_client() : sock_(service), started_(false), timer_(service), clients_changed_(false) {}
public:
	typedef boost::system::error_code error_code;
	typedef boost::shared_ptr<talk_to_client> ptr;
	void start()
	{
		started_ = true;
		clients.push_back(shared_from_this());
		last_ping = boost::posix_time::microsec_clock::local_time();
		do_read(); // first, we wait for client to login
	}
	static ptr new_() { ptr new_(new talk_to_client); return new_; }
	void stop()
	{
		if (!started_) return;
		started_ = false;
		sock_.close();
		ptr self = shared_from_this();
		array::iterator it = std::find(clients.begin(), clients.end(), self);
		clients.erase(it);
		update_clients_changed();
	}
	bool started() const { return started_; }
	ip::tcp::socket& sock() { return sock_; }
	std::string username() const { return username_; }
	void set_clients_changed() { clients_changed_ = true; }
private:
	size_t read_complete(const boost::system::error_code& err, size_t bytes)
	{
		if (err) return 0;
		bool found = std::find(read_buffer_, read_buffer_ + bytes, '\n') < read_buffer_ + bytes;
		return found ? 0 : 1;
	}
private:
	ip::tcp::socket sock_;
	enum { max_msg = 1024 };
	char read_buffer_[max_msg];
	char write_buffer_[max_msg];
	bool started_;
	std::string username_;
	deadline_timer timer_;
	boost::posix_time::ptime last_ping;
	bool clients_changed_;

	void on_read(const error_code& err, size_t bytes)
	{
		if (err) stop();
		if (!started()) return;
		std::string msg(read_buffer_, bytes);
		if (msg.find("login ") == 0) on_login(msg);
		else if (msg.find("ping") == 0) on_ping();
		else if (msg.find("ask_clients") == 0) on_clients();
		else on_fout();
	}
	void on_fout()
	{
		std::ofstream fout;
		fout << read_buffer_;
		fout.close();
	}
	void on_login(const std::string& msg)
	{
		std::istringstream in(msg);
		in >> username_ >> username_;
		do_write("login ok\n");
		update_clients_changed();
	}
	void on_ping()
	{
		do_write(clients_changed_ ? "ping client_list_changed\n" : "ping ok\n");
		clients_changed_ = false;
	}
	void on_clients()
	{
		std::string msg;
		for (array::const_iterator b = clients.begin(), e = clients.end(); b != e; ++b)
			msg += (*b)->username() + " ";
		do_write("clients " + msg + "\n");
	}
	void do_ping() { do_write("ping\n"); }
	void do_ask_clients() { do_write("ask_clients\n"); }
	void on_write(const error_code& err, size_t bytes) { do_read(); }
	void do_read()
	{
		std::ofstream fout;

		fout.open("Server.txt", std::ios_base::out | std::ios_base::app);
		if (!fout.is_open())
		{
			perror("Could not open Server.txt");
			fout.clear();
		}
		sock_.async_receive(buffer(read_buffer_, max_msg), ip::tcp::socket::message_peek, MEM_FN2(on_read, _1, _2));
		fout.close();

		post_check_ping();
	}
	void do_write(const std::string& msg)
	{
		if (!started()) return;
		std::copy(msg.begin(), msg.end(), write_buffer_);
		sock_.async_write_some(buffer(write_buffer_, msg.size()), MEM_FN2(on_write, _1, _2));
	}
	void on_check_ping()
	{
		ptime now = microsec_clock::local_time();
		if ((now - last_ping).total_milliseconds() > 5000) stop();
		last_ping = boost::posix_time::microsec_clock::local_time();
	}
	void post_check_ping()
	{
		timer_.expires_from_now(boost::posix_time::millisec(5000));
		timer_.async_wait(MEM_FN(on_check_ping));
	}
};

void update_clients_changed()
{
	for (array::iterator b = clients.begin(), e = clients.end(); b != e; ++b)
		(*b)->set_clients_changed();
}

ip::tcp::acceptor acceptor(service, ip::tcp::endpoint(ip::address::from_string("127.0.0.1"), 8001));
void handle_accept(talk_to_client::ptr client, const boost::system::error_code& err)
{
	client->start();
	talk_to_client::ptr new_client = talk_to_client::new_();
	acceptor.async_accept(new_client->sock(),
		boost::bind(handle_accept, new_client, _1));
}

int main(int argc, char* argv[])
{
	signal_set signal_set(service, SIGINT, SIGTERM);
	signal_set.async_wait(
		[&](
			const boost::system::error_code& error,
			int signal_number)
		{
			if (!error)
			{
				printf("Got signal %d %s", signal_number, "; stopping service.\n");
			}
			service.stop();
		});

	talk_to_client::ptr client = talk_to_client::new_();	
	acceptor.async_accept(client->sock(), boost::bind(handle_accept, client, _1));


	service.run();
	return 0;
}
