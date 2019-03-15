#ifndef __UTILES_H__ 
#define __UTILES_H__ 

#include <iostream>
#include <unistd.h>
#include <queue>
#include <time.h>
#include <mutex>
#include <signal.h>
#include <pthread.h> 
#include <stdio.h>
#include <unordered_map>
#include <string>
#include <string.h>
#include <fcntl.h>
#include <vector>
#include <errno.h>
#include <stdlib.h>
#include <sstream>
#include <dirent.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <sys/stat.h>
#define LOG(...) do{fprintf(stdout,__VA_ARGS__);fflush(stdout);}while(0) //指定写到某目录 

#define MAX_HEADER  4096
#define MAX_PATH 256
#define WWWROOT "www"
#define MAX_BUFF 4096

std::unordered_map<std::string,std::string> g_err_desc = {
  {"200", "OK"},
  {"400", "Bad Request"},
  {"403", "Forbidden"},
  {"404", "Not Find"},
  {"405", "Method Not Allowed"},
  {"413", "Requst Entity Too Large"},
  {"500", "Internal Server Error"}
};

std::unordered_map<std::string, std::string> g_file_type = {
  {"txt", "apllication/octet-stream"},
  {"html", "text/html"},
  {"htm",  "text/htm"},
  {"jpg",  "image/jpeg"},
  {"zip",  "application/zip"},
  {"mp3",  "audio/mpeg"},
  {"unknow","apllication/octet-stream"}
};

//公用接口工具
class Utils
{
//提供一些公用的功能接口：字符串格式
public:
	static int Split(std::string &src,const std::string &seg,std::vector<std::string> &vec)
	{
		int num = 0; //分隔数量
    size_t idx = 0;//遍历计数
		size_t pos; //目标位置
		
		while(idx < src.size())
		{
			pos = src.find(seg,idx);
			if(pos == std::string::npos)
			{
				break;
			}
			
			vec.push_back(src.substr(idx,pos-idx));
			num++;
			idx = pos +seg.size();
    }
    
    //最后一个字段后面没有\r\n
		if(idx<src.size())
		{
			vec.push_back(src.substr(idx));
			num++;
		}
	
		return num;
	}

  //通过错误码获取错误信息
	static const std::string GetErrDesc(const std::string& code)
	{
		auto it = g_err_desc.find(code);
		if(it == g_err_desc.end())
		{
			return "Unknow Error Code";
		}

		return it->second;
	}
	
	static void TimeToGMT(time_t t,std::string &gmt)
	{

		struct tm *gm = gmtime(&t);//gmtime直接将当前时间转换为格林威治时间，并且转换出来的是一个结构体，有年月日，时分秒， 一周的第一天，一年的第几天等，
		char tmp[128] = {0};
		int len = strftime(tmp , 127,"%a %d %b %Y %H:%M:%S GMT",gm);//将结构体的时间按照一定格式，转换为特定时间,返回字符串实际长度
		gmt.assign(tmp,len);
	
	}	
	
	static void DigitToStr(int64_t num, std::string &str)
	{
    //stringstream 将放进的数字转换为C格式字符串
		std::stringstream ss;
		ss << num; 
		str = ss.str();
	}
	
  static int64_t StrToDigit(const std::string &str)
  {
    int64_t num ;
    std::stringstream ss;
    ss << str;
    ss >> num; //内部模板实现的
    return num;
  }

  static void MakeETag(int64_t size,int64_t ino,int64_t mtime ,std::string &etag)
	{
		std::stringstream ss;

    //"ino-size-mtime"
		ss<<"\"";
		ss<<std::hex<<ino;
		ss<<"-";
		ss<<std::hex<<size;
		ss<<"-";
		ss<<std::hex<<mtime;
	
    ss<<"\"";
		etag = ss.str();
	
	}

  static void GetMine(const std::string &file,std::string &mine)
  {
    size_t pos = file.rfind(".");
    if(pos == std::string::npos)
    {
      mine = g_file_type["unknow"];
      return;
    }

    std::string last_type = file.substr(pos+1);
    auto it = g_file_type.find(last_type);
    if(it == g_file_type.end())
    {
      mine = g_file_type["unknow"];
      return;
    }
    else 
    {
      mine = it->second; 
    }
  }
};

//请求解析结果
class RequestInfo
{
//包含request解析出的的请求信息
public:
	std::string _method;//请求方法
	std::string _version;//协议版本
	std::string _path_info;//资源路径
	std::string _path_phys;//资源实际具体路径
	std::string _query_string;//客户端提供的查询字符串
	std::unordered_map<std::string,std::string> _hdr_pair; //解析出头部的键值对
	struct stat _st;//stat结构体，通过文件名获取文件详细信息
public:
	std::string _err_code;//错误码
	void SetErrCode(const std::string code)
	{
		_err_code = code;
	}

  //判断请求类型
	bool RequestIsCGI()
	{
		if((_method == "POST") || ((_method == "GET" ) && (!_query_string.empty())))
		{
			return true;
		}
		return false;
		
	}
	
};


//http请求
class HttpRequest
{
//http数据接收接口
//http数据解析接口
//对外提供能够获取处理结果的接口
private:
	int _cli_sock;
	std::string _http_header;
	
	
public:
	HttpRequest(int sock):_cli_sock(sock){}
	//接收http请求头
	bool RecvHttpHeader(RequestInfo& info)
	{
		char buf[MAX_HEADER];
		//接收在当前套接字缓冲区,MSG_PEEK指针指向缓冲区的指针
		while(1)
		{
			int ret = recv(_cli_sock,buf,sizeof(buf),MSG_PEEK);
      //接收数据放在套接字缓冲区，可能头部与正文大小在sizeof（buf）内，可能一次性把正文也取走了
      //所以需要探测\r\n\r\n,返回前面即可，所以通过MSG_PEEk探测性的查看缓冲区而不取走数据
      //可能会失败：recv=0就是对端关闭连接  【程序里怎样查看对端关闭连接？recv  = 0，send触发sigpad信号】
			if(ret <= 0)
			{
        //此时是阻塞的，当数据就绪时才会读取，就绪与读取的过程有一段等待的时间
        //这段等待时间是会被信号打断的，万一被信号打断就报EINTR，表示被信号打断，
        //只需要重新读取就好，EAGAIN只是在非阻塞情况的错误，
        //就是数据未就绪就读取就会报错，只有非阻塞情况会报EAGAIN错误
        if(errno == EINTR || errno == EAGAIN)
        {
          //认为这个错误可以原谅，重新读取就好
					continue;
				}
				info.SetErrCode("500");//读取数据失败，认为是服务端错误
				return false;
			}

			char *ptr = strstr(buf,"\r\n\r\n");
			if((ptr == NULL)&&(ret == MAX_HEADER))
			{
				info.SetErrCode("413");//由于请求的实体过大，服务器无法处理，因此拒绝请求
				return false;
				
			}
      else if((ptr == NULL) && ret < MAX_HEADER)
      {
        usleep(1000);
        continue; //可能数据没读取完
      }

			int hdr_len = ptr - buf;
			_http_header.assign(buf,hdr_len);
			recv(_cli_sock,buf,hdr_len+4,0); //将头部从缓冲区移除，只剩正文，0阻塞
	    LOG("header:[%s]\n",_http_header.c_str());
      break;
		}
			return true;
	}

	bool PathIsLegal(RequestInfo &info)
	{
    //path:  /upload
    //phys: ./www/upload
		std::string path = WWWROOT + info._path_info;
		if(stat(path.c_str(),&info._st)<0) //0成功
		{
			info._err_code = "404";
			return false;
		}
		
    char tmp[MAX_PATH] = {0};
		//realpath:将一个路径转换()为绝对路径，若地址不存在，就会造成段错误，
    //char *realpath(const char *path, char *resolved_path)
		//成功则返回指向resolved_path的指针，失败返回NULL，错误代码存于errno
    realpath(path.c_str(),tmp);
		info._path_phys = tmp;
		
    //?
		if(info._path_phys.find(WWWROOT) == std::string::npos)
		{
			info._err_code = "403"; //服务器理解请求客户端的请求，但是拒绝执行此请求
			return false;
		}
		return true;
	}

	bool ParseFirstLine(std::string &first_line, RequestInfo &info)
	{
		std::vector<std::string> line_list;
		if(Utils::Split(first_line, " ", line_list) != 3)
		{
			info._err_code = "400"; //客户端请求错误，服务器无法理解
			return false;
		}
		
		std::string url;
		info._method = line_list[0];
		url = line_list[1];
		info._version = line_list[2];
		
		if(info._method !="GET" && info._method != "POST" && info._method != "HEAD")
		{
			
			info._err_code = "405"; //客户端请求中的方法被禁止
			return false;
		}
		
		if(info._version !="HTTP/0.9" && info._version != "HTTP/1.0" && info._version != "HTTP/1.1")
		{
			
			info._err_code = "400"; //客户端请求错误，服务端无法理解
			return false;
		}
		
		//url  www/load/../..?key=val&key=val
		//realpath:将一个路径转换为绝对路径，若地址不存在，就会造成段错误，
		size_t pos;
		pos = url.find("?");
		if(pos == std::string::npos)
		{
			info._path_info = url;
		}
		else{
			info._path_info = url.substr(0,pos);
			info._query_string = url.substr(pos+1);
		}
		
		return PathIsLegal(info);
	}

  //解析请求头部
	bool ParseHttpHeader(RequestInfo& info)
	{
		
		//请求方法 url 版本\r\n
		//键值对key: val\r\nkey: val
		
		//以\r\n进行分隔，取出字符串
		std::vector<std::string> hdr_vector;
		Utils::Split(_http_header,"\r\n",hdr_vector);
		
		//解析首行
		if(ParseFirstLine(hdr_vector[0],info) == false)
		{
			return false;
		}

		for(size_t i = 1; i<hdr_vector.size() ; ++i)
		{
			size_t pos = hdr_vector[i].find(": ");
			info._hdr_pair[hdr_vector[i].substr(0,pos)] = hdr_vector[i].substr(pos+2);
		}
	
		return true;
	}
	
};

//请求响应
class HttpResponse
{
//文件请求（完成文件下载，列表功能）接口
//CGI请求接口，调用外部程序上传功能
private: //信息表示文件唯一
	int _cli_sock;
	
	//ETag: "inode-fsize-mtime"\r\n
	std::string _etag;//表明文件是否是源文件，是否修改过，没修改过，再次下载就不会返回文件了，下载没意义，提高http效率
	std::string _mtime;//文件最后一次修改时间
	std::string _date; //系统响应时间
	std::string _fsize; //文件大小
	std::string _type_mine; //文件类型
public:

	HttpResponse(int sock):_cli_sock(sock){}

	bool InitResponse(RequestInfo &req_info)//初始化一些响应信息
	{
	/*	req_info._st.st_size;
		  req_info._st.st_ino;  //节点号
		  req_info._st.st_mtime;
  */
    //Last-Modify:
		Utils::TimeToGMT(req_info._st.st_mtime,_mtime);
    
		//Etag:数字转换字符串太长不美观，转换16进再转字符串
		Utils::MakeETag(req_info._st.st_size, req_info._st.st_ino,  req_info._st.st_mtime, _etag);
		
    //Data:
		time_t t = time(NULL);
		Utils::TimeToGMT(t,_date);

    //文件大小
    Utils::DigitToStr(req_info._st.st_size, _fsize);
	
    //文件类型
    Utils::GetMine(req_info._path_phys,_type_mine);
		return true;
	}

  //普通传输文件
  bool SendData(const std::string &buf)
  {
    if(send(_cli_sock,buf.c_str(),buf.length(),0)<0)
    {
      return false;
    }
    return true;
  }

  //分块传输
  bool SendChunkData(const std::string &buf)
  {
  
      //  05\r\n
      //  hello
      //  \r\n
    
      //  最后一个分块特殊
      //  0\r\n\r\n
    
      if(buf.empty())
      {
        return SendData("0\r\n\r\n");
      }

      std::stringstream ss;
      ss << std::hex << buf.length();
      ss << "\r\n";

      SendData(ss.str()); //长度
      SendData(buf);   //数据
      SendData("\r\n");

      return true;
  }

	//文件列表（文件目录即路径）
	bool ProcessList(RequestInfo& info)
	{
    //组织头部信息
    //首行 
    //Content-Type: text/html\r\n
    //Etag: \r\n
    //Data: \r\n
    //Connection: close\r\n\r\n   //就是接受完请求就关闭链接，因为是短链接
    //数据可能很大，不可能一次性传输，需要分块处理，Http 1.1版本中才有这个：
    //Transfer-Encoding: chunked\r\n  就是说只告诉这一块数据大小，不会知道响应总大小
    //不分块也可以，将正文全部发送，大不了失败，关闭链接就好 ，因为http是软连接，一旦对方接受到0，就认为接收完毕
    //正文：每一个目录下的文件都要组织一个html信息
   
    //头部
    std::string rsp_header;
    rsp_header = info._version + " 200 OK\r\n";

    rsp_header += "Content-Type: text/html;charset=UTF-8\r\n";
    rsp_header += "Connection: close\r\n";
    if(info._version == "HTTP/1.1")
    {
      rsp_header += "Transfer-Encoding: chunked\r\n";
    }
    rsp_header += "ETag: "+ _etag + "\r\n";
    rsp_header += "Last-Modifyed: " + _mtime + "\r\n";
    rsp_header += "Data: "+ _date + "\r\n\r\n";
    
    //正常传输
    SendData(rsp_header);
    LOG("File rsp:[%s]\n",rsp_header.c_str());

    //正文数据可能比较大，所以采取分块传输
    std::string rsp_body;
    rsp_body = "<html><head>";
    rsp_body += "<title>Welcome to my zone！" + info._path_info + "</title>";
    rsp_body += "<meta charset='UTF-8'>";
  
    rsp_body += "</head><body>";
    rsp_body += "<h1>Welcome to my zone！" + info._path_info + "</h1>"; 
    
    //新添加上传功能的显示信息
    
    //form表单：表明这是一个上传，这是我要提交的一个数据，
    //action：这个上传操作请求的是是服务端哪个cgi程序，因为提交的时候可以用post。
    //也可以get，但是我们用sgi处理，所以告诉是那个cgi程序，也就是表单执行的动作，或者执行程序的路径，
    //method：方法在上传选择post，较安全，数据在地址栏不显示的，
    //还必须结合enctypa，才是上传文件

    rsp_body += "<form action='/upload' method='POST' enctype='multipart/form-data'>";
    //选择文件,name表示属性
    rsp_body += "<input type='file' name='FileUpLoad' />";
    //上传窗口，命名上传文件，默认submit
    rsp_body += "<input type='submit' value='上传文件' />";
    rsp_body += "</form>";
    //横线
    rsp_body += "<hr />";
    //内部与<li>的作用构成有序列表
    rsp_body += "<ol>";
    SendChunkData(rsp_body);

    //获取目录下的每一个文件，组织html响应信息，chunked传输:每次发送数据前，都应该告诉对方，发送的数据有多长 
    //应该先判断，但是一般都是1.1版本，所以此处不用
  
    //浏览信息 scandir(1,path,2,结构体dirent类型的三级指针（指向目录下每一个文件的信息），\
    //3,函数指针：fileter可自己定义，返回值int，0：不过虑，这个文件存储，保存到三级指针里，1，过滤不存储，即不获取，\
    //4，排序，主要两种（alphasort,versionsort,）) ,返回值为浏览的文件数量，
    //readdir 结构里有这个结构体，结构体有不带路径的文件名，通过文件名加上前面路径，构成全路径文件，\
    //通过stat获取到信息：大小，修改时间，文件类型，组织完后，进行返回。
      
    //int filter（struct *dirent） //过滤 .目录
    //{
    // if(strcmp(dirent->d_name, "."))
    // {
    //  return 1;
    // }
    // else 
    // {
    // return 0;
    // }
    //}
      
    struct dirent **p_dirent = NULL;  //为了给二级指针存储内存，出现三级指针
    //获取目录下的每一个文件，组织出html信息，chunk传输
    int num = scandir(info._path_phys.c_str(),&p_dirent,0,alphasort);

    for(int i = 0; i<num; ++i)
    {
      std::string file_html;
      
      std::string file; 
      file = info._path_phys + p_dirent[i]->d_name ; //文件全路径
      struct stat st;
      if(stat(file.c_str(),&st) < 0 )
      {
         continue ;
      }
        
      std::string mtime; 
      Utils::TimeToGMT(st.st_mtime, mtime);
      std::string type_mine;
      Utils::GetMine(p_dirent[i]->d_name, type_mine);
      std::string fsize;
      Utils::DigitToStr(st.st_size / 1024, fsize ); //默认M，改成kbytes；

      file_html += "<li><strong><a href = '"+ info._path_info;
      file_html += p_dirent[i]->d_name ;
      file_html += "'>";
      file_html += p_dirent[i]->d_name ;
      file_html += "</a></strong><br /><small>";
      file_html += "Modified: " + mtime + "<br />";
      file_html += type_mine + "-" + fsize + " kbytes";
      file_html += "<br /><br /></small></li>";
      SendChunkData(file_html);
    }
  
		rsp_body = "</ol><hr /></body></html>";  
		SendChunkData(rsp_body);
		SendChunkData(""); //分块传输最后为空，则返回\r\n\r\n,结束标识
    
		return true;
	}

   bool BreakPointResume(RequestInfo& info)
  {
        std::string if_range = info._hdr_pair.find("If-Range")->second;
        auto it = info._hdr_pair.find("Range");
        if(it == info._hdr_pair.end()){
            return false;
        }
        std::string bytes = it->second;
        size_t pos = bytes.find("bytes=");
        size_t post = bytes.find('-');
        if(post == std::string::npos)
            return false;
        //from pos+6 cai pos1-pos  
        std::string start = bytes.substr(pos+6,post-(pos+6));
        //cerr << "start: " << start << endl;
        std::string end = bytes.substr(post+1);
        //cerr << "end: " << end << endl;
        int64_t finnal;
        if(end.empty()){
            finnal = info._st.st_size-1;
        }
        else{
            finnal = Utils::StrToDigit(end);
        }
        int64_t begin = Utils::StrToDigit(start);
        //cerr << "begin: " << begin << endl;
        //cerr << "finnal: " << finnal << endl;
        size_t byte = finnal - begin + 1;
        //cerr << "byte: " << byte << endl;
        
        //组织html头部
  
        std::string rsp_header = info._version + " 206 PARTY_CONTENT\r\n";
        rsp_header += "Content-Type: application/octet-stream\r\n"; 
        //标志文件是否被修改
        rsp_header += "Etag: " + _etag + "\r\n";
        //文件大小
        std::string slen;
        Utils::DigitToStr(byte,slen);
        rsp_header += "Content-Length: " + slen + "\r\n"; 
        //file size
        int64_t fsize = info._st.st_size;
        Utils::DigitToStr(fsize,slen);

        Utils::DigitToStr(begin,start);
        //finnal = begin + rlen*len - 1; 
        Utils::DigitToStr(finnal,end);

        rsp_header += "Content-Range: bytes " + start + "-" + end + "/" + slen + "\r\n";
        rsp_header += "Accept-Ranges: bytes\r\n";
        rsp_header += "Last-Modified: " + _date + "\r\n\r\n";
        SendData(rsp_header);
        std::cout << rsp_header << std::endl;
        //发送文件
        int fp = open(info._path_phys.c_str(),O_RDONLY);
        if(fp < 0){
            info._err_code = "400";
            std::cerr << "open error!" << std::endl;
            return false;
        }
        lseek(fp,begin,SEEK_SET);
        //cerr << "seek: " << ftell(fp) << endl; 
        char tmp[MAX_BUFF];
        size_t clen = 0;
        size_t rlen = 0;
        while(clen < byte){
            int len = (byte - clen) > (MAX_BUFF-1) ? (MAX_BUFF-1) : (byte-clen);
            rlen = read(fp,tmp,len);
            //clen += (rlen*len);
            clen += rlen;
            send(_cli_sock,tmp,rlen,0);
        }
        close(fp);
        return true;
    }

	//文件下载(文件名称)
	bool ProcessFile(RequestInfo& info)
	{
			auto it = info._hdr_pair.find("If-Range");
      if(it != info._hdr_pair.end()){
       // cerr << "========================" << endl;
       // cerr << it->second << endl;
        std::string if_range = it->second;
        std::string etag_tmp = _etag;
        // cerr << etag_tmp << endl;
        if(etag_tmp == if_range){
       //执行断点续传功能
         if(BreakPointResume(info)){
              return true;
          }
           info._err_code = "404";
           ErrHandler(info);
           return false;
      }
    }

		//头部
		std::string rsp_header;
		rsp_header = info._version + " 200 OK\r\n";
		// rsp_header += "Content-Type: " + _type_mine + "\r\n"; //决定服务端如何处理响应的数据
		rsp_header += "Content-Type: application/octet-stream;charset=UTF-8\r\n";
		//rsp_header += "Connection: close\r\n"; //短链接
		rsp_header += "Content-Length: " + _fsize + "\r\n";
    rsp_header += "Accept-Ranges: bytes\r\n";
		rsp_header += "ETag: "+ _etag + "\r\n";
		rsp_header += "Last-Modifyed: " + _mtime + "\r\n";
		rsp_header += "Date: "+ _date + "\r\n\r\n";
    
		//正常传输
		SendData(rsp_header);
		LOG("File rsp:[%s]\n",rsp_header.c_str());

		//文件数据 
		int fd = open(info._path_phys.c_str(),O_RDONLY);
		if (fd < 0)
		{
			info._err_code = "400";// 坏的请求
			ErrHandler(info);
			return false;
		}

		int rlen = 0;
		char buf[MAX_BUFF];
		while((rlen = read(fd,buf,sizeof(buf))) > 0 )
		{
			//不能用char* ，因为可能传输的就是多个0，这样会导致对端收不到，会认为没有信息了，就关闭了，
			//导致程序会崩溃，（对方关闭链接，send会收到sigpipe信号，触发终止进程，）所以需要忽略信号
			// buf[rlen] = '\0';
			// SendData(buf);
			send(_cli_sock,buf,rlen,0);
		}
  
		close(fd); //下载一个可以不管，但是如果下载多个，描述符用完了就不能在打开文件了
		return true;
	}
	
  
	//CGI请求处理（文件名称）
	bool ProcessCGI(RequestInfo& info)
	{
		//【程序运行起来需要获取很多东西，请求头信息，正文，头信息已经解析过了，让子进程方便的获取这些信息，并不推荐全部用管到
		//让头在解析一遍，太麻烦，所以采用环境变量传递头信息】
		//流程
		//使用外部程序完成上传cgi请求处理  --- 文件上传
		//将http头信息和正文全部交于子进程处理（不推荐放在一块，因为需要重复解析）
		//所以使用环境变量传递头信息
		//使用管道传递正文数据
		//使用管道接受cgi程序处理结果
		//流程：创建管道，创建子进程，设置子进程环境变量，程序替换
		int in[2];//父进程向子进程传输正文
		int out[2];//用于从子进程读取处理结果 
    
		if(pipe(in) || pipe(out))
		{
			info._err_code = "500";//服务器内部错误
			ErrHandler(info);
			return false; 
		}

		int pid = fork();
		if(pid < 0)
		{
			info._err_code = "500";
			ErrHandler(info);
			return false;
		}
		else if(pid == 0)
		{
			// setenv(const char *name,const char * value,int overwrite);
			// setenv()用来改变或增加环境变量的内容。参数name为环境变量名称字符串。
			// 参数 value则为变量内容，参数overwrite用来决定是否要改变已存在的环境变量。
			// 如果没有此环境变量则无论overwrite为何值均添加此环境变量。若环境变量存在，
			// 当overwrite不为0时，原内容会被改为参数value所指的变量内容；
			// 当overwrite为0时，则参数value会被忽略。返回值 执行成功则返回0，有错误发生时返回-1。
			//请求首行
			setenv("METHOD", info._method.c_str(),1);
			setenv("VERSION", info._version.c_str(),1);
			setenv("PATH_INFO", info._path_info.c_str(),1);
			setenv("QUERY_STRING", info._query_string.c_str(),1);
			//正文
			for(auto it = info._hdr_pair.begin(); it != info._hdr_pair.end(); ++it)
			{
					setenv(it->first.c_str(), it->second.c_str(),1);
			}

			close(in[1]);//关闭写
			close(out[0]);//关闭读
			//开源项目方法：
			//dup(oldfd):对传入的文件描述符进行复制，返回新的文件描述符
			//dup2(oldfd,newfd),newfd指定新的描述符的值，如果已经打开，则现将其关闭。
			dup2(in[0],0); //将标准输入,重定向in[0],子进程直接在标准输入读取正文数据
			dup2(out[1], 1); //将标准输出，重定向out[1]，父进程读取正文，直接在标准输出读取就好，即子进程直接打印处理结果传递给父进程
      
			execl(info._path_phys.c_str(),info._path_phys.c_str(),NULL);//失败直接退出
			exit(0);
		}
		else  //父进程通过in管道将正文传给子进程，然后通过out管道读取子进程处理结果直到返回0，将处理结果组织http数据，响应客户端 
		{
			close(in[0]);
			close(out[1]);

			//父进程通过in管道将正文传给子进程，
			//通过out管道读取子进程的处理结果直到返回
			//将处理结果组织http数据，响应给客户端
			auto it  = info._hdr_pair.find("Content-Length");
			//为空表示没有，不需要提交正文数据给子进程
			if(it != info._hdr_pair.end())
			{
				char buf[MAX_BUFF] = {0};
				int64_t content_len = Utils::StrToDigit(it->second);
				int tlen = 0;
				while(tlen < content_len)
				{
					int len =  MAX_BUFF > (content_len - tlen)?(content_len - tlen): MAX_BUFF;
					int rlen = recv(_cli_sock,buf,len,0);
					if(rlen <= 0)
					{
						//响应错误给客户端
						// info._err_code = "";
						return false;
					}
					if(write(in[1], buf, rlen) < 0)
					{
						return false;
					}
					tlen += rlen;
				}
			}
			//然后等待子进程处理，通过out管道读取子进程处理结果直到返回0，
			//将处理结果组织http数据，响应客户端
     
			//头部
			std::string rsp_header;
			rsp_header = info._version + " 200 OK\r\n";
			rsp_header += "Content-Type: text/html;charset=UTF-8\r\n"; //决定服务端如何处理响应的数据
			rsp_header += "Connection: close\r\n"; //短链接
			rsp_header += "ETag: "+ _etag + "\r\n";
			rsp_header += "Last-Modifyed: " + _mtime + "\r\n";
			rsp_header += "Date: "+ _date + "\r\n\r\n";
    
			//正常传输
			SendData(rsp_header);
			LOG("File rsp:[%s]\n",rsp_header.c_str());
   
			while(1)
			{
				char buf[MAX_BUFF] = {0};
				int rlen = read(out[0],buf,MAX_BUFF);
				if(rlen == 0)
				{
					break;
				}
				send(_cli_sock,buf,rlen,0);
				LOG("CGI rsp body:[%s]\n",buf);
			}

    
			close(in[1]);//关闭写
			close(out[0]);//关闭读
		}
		return true;
    }

	bool CGIHandler(RequestInfo& info)
	{
		InitResponse(info);//初始化cgi响应信息
		ProcessCGI(info);//指向cgi响应
		return true;
	}

	//st结构体中的mode，有比较多的的选项，有表示是否是常规文件以及目录等
	bool FileIsDir(RequestInfo &info)
	{
		if(info._st.st_mode & S_IFDIR)
		{
			std::string path = info._path_info;
			if(path[path.length()-1] != '/')
			{
				info._path_info.push_back('/');
			}
			std::string phys = info._path_phys; 
			if(phys[phys.length()-1] != '/')
			{
				info._path_phys.push_back('/');
			}
			return true;
		}
		return false; 

	}

	bool FileHandler(RequestInfo& info)
	{
		InitResponse(info);//初始化file响应信息
		if(FileIsDir(info))//判断请求文件是否是目录文件
		{
			ProcessList(info);//执行文件列表响应
		}
		else
		{
			ProcessFile(info);//执行文件下载响应
		}
		return true;
	}

	bool ErrHandler(RequestInfo& info)//错误响应
	{
    //响应头信息
		std::string rsp_header;
	
    //首行 版本  状态码 状态描述\r\n
		//头部 长度： Content-Length    当前系统时间:Date
		//空行             
		//正文 rsp_body = "<html><body><h1> 404; <h1></body></html>"  //一般错误没有正文 有的话：html标签  正文标签 标题
		
    //响应正文 <html><body><h1> 404;<h1></body></html>
		std::string rsp_body;
		rsp_body = "<html><body><h1>"+info._err_code;
		rsp_body += "<h1></body></html>";
    
    //首行：版本 状态码 描述信息（code对应的描述信息）
		rsp_header = info._version + " " + info._err_code + " ";
		rsp_header += Utils::GetErrDesc(info._err_code)+"\r\n";
		
    //日期:Date:
		time_t t = time(NULL);//系统当前时间戳
		std::string gmt;
		Utils::TimeToGMT(t,gmt); //响应的都是格林威治时间,需要进行转换 （周，日 月 年 时：分：秒 GMT）
		rsp_header += "Date: "+gmt + "\r\n";
		
	
    //正文长度 :Content-Length
		std::string content_length;
		Utils::DigitToStr(rsp_body.size(),content_length);
		rsp_header += "Content-Length: " + content_length + "\r\n\r\n";
		
    //发送头部
		send(_cli_sock,rsp_header.c_str(),rsp_header.size(),0);
    //发送正文
		send(_cli_sock,rsp_body.c_str(),rsp_body.size(),0);
	  
		return true;
	}

};

#endif
