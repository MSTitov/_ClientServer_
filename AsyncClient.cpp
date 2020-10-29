#include "stdafx.h"
#include <fstream>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/core/noncopyable.hpp>


using namespace boost::asio;
io_service service;

#define MEM_FN(x) boost::bind(&self_type::x, shared_from_this())
#define MEM_FN1(x,y) boost::bind(&self_type::x, shared_from_this(),y)
#define MEM_FN2(x,y,z) boost::bind(&self_type::x, shared_from_this(),y,z)

class talk_to_svr : public boost::enable_shared_from_this<talk_to_svr>, boost::noncopyable
{
	typedef talk_to_svr self_type;
	talk_to_svr(const std::string& username) : sock_(service), started_(true), username_(username), timer_(service) {}
	talk_to_svr() : sock_(service), started_(true), timer_(service) {}
	void start(ip::tcp::endpoint ep)
	{
		sock_.async_connect(ep, MEM_FN1(on_connect, _1));
	}
public:
	typedef boost::system::error_code error_code;
	typedef boost::shared_ptr<talk_to_svr> ptr;
	static ptr start(ip::tcp::endpoint ep, const std::string& username)
	{
		ptr new_(new talk_to_svr(username));
		new_->start(ep);
		return new_;
	}
	void start(ip::tcp::endpoint ep, const char& word)
	{
		talk_to_svr();
		write_buffer_[max_msg] = word;
		sock_.async_connect(ep, MEM_FN1(on_conn, _1));
	}
	void stop()
	{
		if (!started_) return;
		started_ = false;
		sock_.close();
	}
	bool started() { return started_; }
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

	void on_connect(const error_code& err)
	{
		if (!err) do_write("login " + username_ + "\n");
		else stop();
	}
	void on_conn(const error_code& err)
	{
		if (!err) do_write_buffer();
		else stop();
	}
	void on_read(const error_code& err, size_t bytes)
	{
		if (err) stop();
		if (!started()) return;
		// process the msg
		std::string msg(read_buffer_, bytes);
		if (msg.find("login ") == 0) on_login();
		else if (msg.find("ping") == 0) on_ping(msg);
		else if (msg.find("clients ") == 0) on_clients(msg);
		else on_fin();
	}
	void on_fin()
	{
		std::ifstream fin;
		fin.clear();
	}
	void on_login()
	{
		do_ask_clients();
	}
	void on_ping(const std::string& msg)
	{
		std::istringstream in(msg);
		std::string answer;
		in >> answer >> answer;
		if (answer == "client_list_changed") do_ask_clients();
		else postpone_ping();
	}
	void on_clients(const std::string& msg)
	{
		std::string clients = msg.substr(8);
		std::cout << username_ << ", new client list:" << clients;
		postpone_ping();
	}

	void do_ping() { do_write("ping\n"); }
	void postpone_ping()
	{
		timer_.expires_from_now(boost::posix_time::millisec(rand() % 7000));
		timer_.async_wait(MEM_FN(do_ping));
	}
	void do_ask_clients() { do_write("ask_clients\n"); }
	void on_write(const error_code& err, size_t bytes) { do_read(); }
	void do_read()
	{
		async_read(sock_, buffer(read_buffer_), MEM_FN2(read_complete, _1, _2), MEM_FN2(on_read, _1, _2));
	}
	void do_write(const std::string& msg)
	{
		if (!started()) return;
		std::copy(msg.begin(), msg.end(), write_buffer_);
		sock_.async_write_some(buffer(write_buffer_, msg.size()), MEM_FN2(on_write, _1, _2));
	}
	void do_write_buffer()
	{
		if (!started()) return;
		sock_.async_send(buffer(write_buffer_, max_msg), ip::tcp::socket::message_peek, MEM_FN2(on_read, _1, _2));
	}
};

int main(int argc, char* argv[])
{

	ip::tcp::endpoint ep(ip::address::from_string("127.0.0.1"), 8001);
	std::ifstream fin;

	fin.open("Client.txt", std::ios_base::in | std::ios_base::app);
	char buf[1024];

	if (!fin.is_open())
	{
		perror("Could not open Client.txt");
		fin.clear();
	}
	while (fin.getline(buf, 1024))	
		talk_to_svr::start(ep, buf);		
	
	boost::this_thread::sleep(boost::posix_time::millisec(100));
	fin.clear();
	fin.close();

	service.run();
	return 0;
}