
#include "root_certificates.hpp"
#include "sbeast.hpp"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include "snetworktypes.h"
#include "sauthstring.h"
#include "shttpparameters.h"
#include "spayload.h"
#include "shttpparameters.h"
#include "urilite.h"
//------------------------------------------------------------------------------

// Report a failure
namespace Ui
{
     inline std::string chunkify(boost::string_view s)
        {
            std::ostringstream ss;
            ss << std::hex << s.size();
            auto result = boost::to_upper_copy(ss.str()) + "\r\n";
            result.append(s.begin(), s.end());
            result += "\r\n";
            return result;
        }
    inline std::string operator+(const std::string& first, boost::string_view second){
        return first + std::string{second.data(),second.length()};
    }
    inline std::string operator+(boost::string_view first, const std::string& second){
        return std::string{first.data(),first.length()} + second;
    }
    std::string 
    inline shttp_fail(beast::error_code ec, char const *what)
    {
        std::string msg=what +std::string(": ")+ ec.message()+ "\n category: "+ ec.category().name();
        std::cout << msg.c_str();
        return msg;
    }
    class shttp_client_ssl_session;
    struct snetwork_scheduler
    {
        net::io_context ioc;
        net::io_service::work work{ioc};
        std::thread t;
        snetwork_scheduler(){
            t=std::thread([=](){
                ioc.run();
            });
        }
        static snetwork_scheduler& instance()
        {
            static snetwork_scheduler sch;
            return sch;
        }
        ~snetwork_scheduler(){
            stop_network_schduler();
        }
        void stop_network_schduler(){
            ioc.stop();
            t.join();
        }
    };
    using APP_READ_HANDLER = std::function<void(beast::error_code ec, std::string)>;
    // Performs an HTTP GET and prints the response
    struct shttp_client_session_base
    {
        
        tcp::resolver resolver_;
        beast::flat_buffer buffer_; // (Must persist between reads)
        http::request<http::string_body> req_;
        // http::response<http::string_body> res_;
        http::response_parser< http::string_body> res_;
        APP_READ_HANDLER app_handler;
        Url url_;
        Parameters params_;
        HttpHeader header_;
        Authentication auth_;
        Payload payload_;
        Port port_{"80"};
        std::string vers_{"1.0"};
        Target target_{"/"};
        Body body_;
        ContentType cont_type_{"text/plain"};
        http::verb verb_{http::verb::get};
        virtual ~shttp_client_session_base(){
            
        }
        shttp_client_session_base(net::io_context& ioc)
        :resolver_(net::make_strand(ioc))
        { }
        void call_app_handler(beast::error_code ec, std::string data)
        {
            app_handler(std::move(ec),std::move(data));
            
        }
        void SetOption(const Url& url)
        {
            url_=url;
        }
        void SetOption(const Parameters& parameters)
        {
            params_=parameters;
        }
        void SetOption(Parameters&& parameters)
        {
            params_=std::move(parameters);
        }
        void SetOption(const HttpHeader& header)
        {
            header_=header;
        }
        // void SetOption(const Timeout& timeout);
        // void SetOption(const ConnectTimeout& timeout);
        void SetOption(const Authentication& auth)
        {
            auth_=auth;
        }
        // void SetOption(const Digest& auth);
        // void SetOption(const UserAgent& ua);
        void SetOption(Payload&& payload)
        {
            payload_=std::move(payload);
        }
        void SetOption(const Payload& payload)
        {
            payload_=payload;
        }
        
        void SetOption(APP_READ_HANDLER handler)
        {
            app_handler=std::move(handler);
        }
        void SetOption(Ui::Port port)
        {
            port_=std::move(port);
        }
        void SetOption(Ui::Target t)
        {
            target_=std::move(t);
        }
        void SetOption(http::verb v)
        {
            verb_=v;
        }
        
        void get()
        {
            run();
        }
        void prepare_req()
        {
             // Set up an HTTP GET request message
            req_.version(vers_.c_str() ? 10 : 11);
            req_.method(verb_);
            req_.target((target_+params_.content).c_str());
            req_.set(http::field::host, url_.c_str());
            req_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            if(!body_.str().empty()){
                req_.set(http::field::body, body_);
                req_.body() = body_;
            }
            req_.set(http::field::content_type,cont_type_);
            req_.prepare_payload();
            for(auto& head:header_){
                req_.set(head.first, head.second); 
            }
            // req_.keep_alive(true);
        }
        virtual void run()=0;
        // void SetOption(const LimitRate& limit_rate);
        // void SetOption(Proxies&& proxies);
        // void SetOption(const Proxies& proxies);
        // void SetOption(Multipart&& multipart);
        // void SetOption(const Multipart& multipart);
        // void SetOption(const NTLM& auth);
        // void SetOption(const bool& redirect);
        // void SetOption(const MaxRedirects& max_redirects);
        // void SetOption(const Cookies& cookies);
        void SetOption(Body&& b)
        {
            body_=std::move(b);
        }
        void SetOption(const Body& b)
        {
            body_=b;
        }
        void SetOption(const ContentType& t){
            cont_type_=t;
        }
        void SetOption(ContentType&& t){
            cont_type_=std::move(t);
        }
        template<typename Cont>
        void sync_read(auto& stream_,auto& buffer_,Cont&& cont){
            beast::error_code ec{};     
            http::response_parser<http::buffer_body> res0;
            http::read_header(stream_, buffer_, res0,ec);
            if(ec){
                return call_app_handler(ec,"read: header"); 
                
            }
            int field_count = 0;
            for(auto const& field : res0.get())
                std::cout << "Field#"
                            << ++field_count << " : " << field.name() << " = " << field.value() << std::endl;

            std::stringstream os;
            while(! res0.is_done())
            {
                char buf[512];
                res0.get().body().data = buf;
                res0.get().body().size = sizeof(buf);
                http::read(stream_, buffer_, res0, ec);
                if(ec == http::error::need_buffer)
                    ec = {};
                if(ec)
                    return call_app_handler(ec,os.str() + "read: body");;
                os.write(buf, sizeof(buf) - res0.get().body().size);
            }
            // http::response_parser<http::string_body> res{std::move(res0)};
            // http::read(stream_, buffer_, res,ec);
            // if(ec){
            //     return call_app_handler(ec,"read: body");
            // }
            // call_app_handler(ec,res.get().body());
            call_app_handler(ec,os.str());
            cont();
            
           
        }
        // void SetOption(const LowSpeed& low_speed);
        // void SetOption(const VerifySsl& verify);
        // void SetOption(const Verbose& verbose);
        // void SetOption(const UnixSocket& unix_socket);
        // void SetOption(const SslOptions& options);

        // Response Delete();
        // Response Download(std::ofstream& file);
        
    };
    class shttp_client_ssl_session :public shttp_client_session_base,public std::enable_shared_from_this<shttp_client_ssl_session>
    {
        
        private:
        
        beast::ssl_stream<beast::tcp_stream> stream_;
        
    public:
        explicit shttp_client_ssl_session(
            net::io_context& ioc,
            ssl::context &ctx)
            : shttp_client_session_base(ioc), stream_(net::make_strand(ioc), ctx)
        {
           port_=Port("443");
           stream_.set_verify_callback([](bool preverified,boost::asio::ssl::verify_context& ctx){return true;});
        }
        bool verify_certificate(bool preverified,boost::asio::ssl::verify_context& ctx)
        {
            return true;
        }
       
    private:
        // Start the asynchronous operation
        void
        run()
        {
            net::post(
                    stream_.get_executor(),
                    [self = shared_from_this()]() mutable {
                        // Set SNI Hostname (many hosts need this to handshake successfully)
                        if (!SSL_set_tlsext_host_name(self->stream_.native_handle(),  self->url_.c_str()))
                        {
                            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
                            // std::cerr << ec.message() << "\n";
                            self->call_app_handler(ec, std::string());
                            return;
                        }
                        self->prepare_req();
                        // Look up the domain name
                        self->resolver_.async_resolve(
                            self->url_.c_str(),
                            self->port_.c_str(),
                            beast::bind_front_handler(
                                &shttp_client_ssl_session::on_resolve,
                                self->shared_from_this()));
                    }
            );
        }

        void
        on_resolve(
            beast::error_code ec,
            tcp::resolver::results_type results)
        {
            if (ec)
                return call_app_handler(ec, "resolve");

            // Set a timeout on the operation
            beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

            // Make the connection on the IP address we get from a lookup
            beast::get_lowest_layer(stream_).async_connect(
                results,
                beast::bind_front_handler(
                    &shttp_client_ssl_session::on_connect,
                    shared_from_this()));
        }

        void
        on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
        {
            if (ec)
                return call_app_handler(ec, "connect");

            // Perform the SSL handshake
            stream_.async_handshake(
                ssl::stream_base::client,
                beast::bind_front_handler(
                    &shttp_client_ssl_session::on_handshake,
                    shared_from_this()));
        }

        void
        on_handshake(beast::error_code ec)
        {
            if (ec)
                return call_app_handler(ec, "handshake");

            // Set a timeout on the operation
            beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

            // Send the HTTP request to the remote host
            http::async_write(stream_, req_,
                              beast::bind_front_handler(
                                  &shttp_client_ssl_session::on_write,
                                  shared_from_this()));
        }
        void
        on_write(
            beast::error_code ec,
            std::size_t bytes_transferred)
        {
            boost::ignore_unused(bytes_transferred);

            if (ec)
                return call_app_handler(ec, "write");

            // Receive the HTTP response
            // sync_read(stream_,buffer_,[&](){
            //     beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));
            //     // Gracefully close the stream
            //     stream_.async_shutdown(
            //         beast::bind_front_handler(
            //             &shttp_client_ssl_session::on_shutdown,
            //             shared_from_this()));
            // });
            

            http::async_read(stream_, buffer_, res_,
                             beast::bind_front_handler(
                                 &shttp_client_ssl_session::on_read,
                                 shared_from_this()));
        }

        void
        on_read(
            beast::error_code ec,
            std::size_t bytes_transferred)
        {
            boost::ignore_unused(bytes_transferred);

            if (ec && (ec!= beast::http::error::partial_message || ec != beast::error::timeout)){
                //  std::string head_message= std::accumulate(begin(res_),end(res_),std::string{},[&,field_count=0](auto sofar,auto& field)mutable{
                //      return sofar +  "Field#" + std::to_string(++field_count )+":"+field.name_string() + "=" + field.value() +"\n";
                //  });
                
                // return call_app_handler(ec, "read\n Reason: " + std::string{res_.reason().data(),res_.reason().length()} + head_message);
                return call_app_handler(ec,res_.get().body());
            }
                

            // Write the message to standard out
            call_app_handler(ec, std::move(res_.get().body()));
            // Set a timeout on the operation
            beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

            // Gracefully close the stream
            stream_.async_shutdown(
                beast::bind_front_handler(
                    &shttp_client_ssl_session::on_shutdown,
                    shared_from_this()));
        }

        void
        on_shutdown(beast::error_code ec)
        {
            if (ec == net::error::eof)
            {
               ec = {};
            }
            if (ec)
                return app_handler(ec, "shutdown");

            // If we get here then the connection is closed gracefully
        }

      
    };

class shttp_client_session : public shttp_client_session_base,public std::enable_shared_from_this<shttp_client_session>
{
    
    beast::tcp_stream stream_;
    
    
public:
    // Objects are constructed with a strand to
    // ensure that handlers do not execute concurrently.
    explicit
    shttp_client_session(net::io_context& ioc)
        :shttp_client_session_base(ioc)
        ,stream_(net::make_strand(ioc))
    {
    }
    
private:
    // Start the asynchronous operation
    void
    run()
    {
       
       net::post(
                    stream_.get_executor(),
                    [self = shared_from_this()]() mutable {
                        self->prepare_req();
                        // Look up the domain name
                        self->resolver_.async_resolve(
                            self->url_.c_str(),
                            self->port_.c_str(),
                            beast::bind_front_handler(
                                &shttp_client_session::on_resolve,
                                self->shared_from_this()));
                    }
            );
    }

    void
    on_resolve(
        beast::error_code ec,
        tcp::resolver::results_type results)
    {
        if(ec)
            return call_app_handler(ec, "resolve");

        // Set a timeout on the operation
        stream_.expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a lookup
        stream_.async_connect(
            results,
            beast::bind_front_handler(
                &shttp_client_session::on_connect,
                shared_from_this()));
    }

    void
    on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
    {
        if(ec)
            return call_app_handler(ec, "connect");

        // Set a timeout on the operation
        stream_.expires_after(std::chrono::seconds(30));

        // Send the HTTP request to the remote host
        http::async_write(stream_, req_,
            beast::bind_front_handler(
                &shttp_client_session::on_write,
                shared_from_this()));
    }

    void
    on_write(
        beast::error_code ec,
        std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if(ec)
            return call_app_handler(ec, "write");
        

        sync_read(stream_,buffer_,[&](){
            beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));
            // Gracefully close the stream
            stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
            if(ec && ec != beast::errc::not_connected)
                return call_app_handler(ec, "shutdown");
        });
        
        // Receive the HTTP response
        // http::async_read(stream_, buffer_, res_,
        //     beast::bind_front_handler(
        //         &shttp_client_session::on_read,
        //         shared_from_this()));
    }

    void
    on_read(
        beast::error_code ec,
        std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if(ec)
            return call_app_handler(ec, "read");

        call_app_handler(ec,std::move(res_.get().body()));

        // Gracefully close the socket
        stream_.socket().shutdown(tcp::socket::shutdown_both, ec);

        // not_connected happens sometimes so don't bother reporting it.
        if(ec && ec != beast::errc::not_connected)
            return call_app_handler(ec, "shutdown");

        // If we get here then the connection is closed gracefully
    }
};



//------------------------------------------------------------------------------
template<typename SESSION,typename HEAD,typename... TAIL>
    void http_get_impl(SESSION& session,HEAD&& head, TAIL&&... tail){
        http_get_impl(session,std::forward<HEAD>(head));
        http_get_impl(session,std::forward<TAIL>(tail)...);
        
    }
    template<typename SESSION,typename HEAD>
    void http_get_impl(SESSION& session,HEAD&& head){
        session->SetOption(std::forward<HEAD>(head));
      
    }
    
    template<typename... ARGS>
    auto http_get(ARGS&&... args){
        auto session=std::make_shared<shttp_client_session>(snetwork_scheduler::instance().ioc);
        http_get_impl(session,std::forward<ARGS>(args)...);
        session->get();
    }
    template<typename... ARGS>
    auto http_s_get(ARGS&&... args){
        // The SSL context is required, and holds certificates
        ssl::context ctx{ssl::context::tlsv12_client};
        // This holds the root certificate used for verification
        load_root_certificates(ctx);
        // Verify the remote server's certificate
        ctx.set_verify_mode(ssl::verify_peer);
        auto session=std::make_shared<shttp_client_ssl_session>(snetwork_scheduler::instance().ioc,ctx);
        http_get_impl(session,std::forward<ARGS>(args)...);
        session->get();
    }

    template<typename... ARGS>
    auto http_get( urilite::uri remotepath, ARGS&&... args){
        auto qstr=remotepath.query_string();
        auto targ= qstr.empty() ? Ui::Target{remotepath.path()}:Ui::Target{remotepath.path()+"?"+remotepath.query_string()};
        if(remotepath.secure()){
           return  http_s_get(Ui::Url{remotepath.host()},Ui::Port(std::to_string(remotepath.port())),targ,std::forward<ARGS>(args)...);
        }
        http_get(Ui::Url{remotepath.host()},Ui::Port(std::to_string(remotepath.port())),targ,std::forward<ARGS>(args)...);
    }

    
    
}
template<typename Handler>
inline auto make_ui_handler(Handler handler){
   return [handler=std::move(handler)](beast::error_code ec, std::string data){
            postandExcuteHandler([handler=std::move(handler),ec=std::move(ec),data=std::move(data)](){
                handler(std::move(ec),std::move(data));
            });
        };
}