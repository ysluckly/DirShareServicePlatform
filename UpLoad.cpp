#include "Utils.hpp"

enum _boundry_type
{
  BOUNDRY_NO = 0,
  BOUNDRY_FIRST,
  BOUNDRY_MIDDLE,
  BOUNDRY_LAST,
  BOUNDRY_BAK //备注
};
//正文数据解析，存储
class UpLoad
{
  private:
    int _file_fd;
    std::string _file_name; 
    int64_t content_len;
    std::string _f_boundry;
    std::string _m_boundry;
    std::string _l_boundry;

  private:
    int  MatchBoundry(char* buf,int blen,int *boundry_pos)
    {
      //--boundry 
      //first ------boundry\r\n
      //middle \r\n------boundry\r\n
      //last   \r\n------boundry--\r\n
      //起始位置匹配first_boundry,strstr可能遇到零停止
      if( !memcmp(buf,_f_boundry.c_str(),_f_boundry.length()))
      {
       *boundry_pos = 0;
        return BOUNDRY_FIRST;
      }
      for(int i = 0; i< blen; ++i)
      {
        if((blen-i) > _m_boundry.length()) 
        { 
          if(!memcmp(buf+i, _m_boundry.c_str(),_m_boundry.length()))
          {
            *boundry_pos = i;
            return BOUNDRY_MIDDLE;
          }else if(!memcmp(buf+i, _l_boundry.c_str(),_l_boundry.length()))
          {
            *boundry_pos = i;
            return BOUNDRY_LAST; 
          }

        }else 
        {
          //防止半个boundry匹配
          //进行部分匹配
          int cmp_len = (blen - i) > _m_boundry.length() ? _m_boundry.length() : (blen - i);
          if(!memcmp( buf +i , _m_boundry.c_str(),cmp_len ))
          {
            *boundry_pos = i;
            return BOUNDRY_BAK;
          }
          if(!memcmp( buf +i , _l_boundry.c_str(),cmp_len ))
          {
            *boundry_pos = i;
            return BOUNDRY_BAK;
          }

        }
      }
      return BOUNDRY_NO; 
    }


    bool GetFileName(char* buf, int *content_pos)
    {
      char* ptr = NULL;
      ptr = strstr(buf, "\r\n\r\n");
      if(ptr == NULL)
      {
        *content_pos = 0;
        return false;

      }
      *content_pos = ptr - buf + 4;
      std::string header;
      header.assign(buf, ptr - buf);

      std::string file_sep = "filename=\"";
      size_t pos = header.find(file_sep);

      if(pos == std::string::npos)
      {
        return false;
      }

      std::string file; 
      file = header.substr(pos + file_sep.length());
      pos = file.find("\"");
      if(pos == std::string::npos)
      {
        return false; 
      }
      file.erase(pos);

      _file_name = WWWROOT;
      _file_name += "/" + file; 
      fprintf(stderr, "upload file:[%s]\n", _file_name.c_str());

      return true;
    }

    bool CreateFile()
    {
      _file_fd = open(_file_name.c_str(), O_CREAT | O_WRONLY, 0664);
      if(_file_fd < 0)
      {
        fprintf(stderr, "open error : %s\n", strerror(errno));
        return false; 
      }
      return true; 
    }

    bool CloseFile()
    {
      if(_file_fd != -1)
      {
        close(_file_fd);
        _file_fd = -1;
      }
      return true; 
    }

    bool WriteFile(char* buf, int len)
    {
      if(_file_fd != -1)
      {
        write(_file_fd, buf, len);
      }
      return true;
    }
  public:
    UpLoad():_file_fd(-1){}
    //初始化boundry信息
    bool InitUpLoadInfo()
    {
      umask(0);
      char* ptr = getenv("Content-Length");
      if(ptr == NULL)
      {
        fprintf(stderr, "have no content-length\n");
        return false;
      }

      fprintf(stderr, "---------upload :%ld\n", content_len);
      content_len = Utils::StrToDigit(ptr);

      ptr = getenv("Content-Type");
      if(ptr == NULL)
      {
        fprintf(stderr,"have no type\n");
        return false; 
      }

      std::string boundry_sep = "boundary=";
      std::string content_type = ptr;
      size_t pos = content_type.find(boundry_sep);
      if(pos == std::string::npos)
      {
        fprintf(stderr,"HAVE NO BOUNDRY\n");
        return false; 
      }

      std::string boundry; 
      boundry = content_type.substr(pos + boundry_sep.length());

      fprintf(stderr, "-----boundry: %s\n", boundry.c_str());
      _f_boundry = "--" + boundry ;
      _m_boundry = "\r\n" + _f_boundry + "\r\n";
      _l_boundry = "\r\n" + _f_boundry + "--";

      return true; 
    }
    //对正文进行处理，将文件进行存储（处理文件上传）
    bool ProcessUpLoad()
    {
      int64_t tlen = 0, blen = 0;
      char buf[MAX_BUFF];
      while(tlen < content_len)
      {
        int len = read(0,buf + blen, MAX_BUFF- blen);
        blen += len ; //当前buf中数据长度
        int boundry_pos; 
        int content_pos; 

        int flag = MatchBoundry(buf, blen , &boundry_pos);
        if(flag == BOUNDRY_FIRST )
        { 
          //1,从boundry获取文件名
          //若成功，创建文件名，打开文件
          //将头信息从buf移除，剩余数据进行下一步匹配
          if(GetFileName(buf,&content_pos))
          {
            CreateFile();
            blen -= content_len;
            memmove(buf,buf+ content_pos, blen);
          }
          else
          {
            if(content_pos == 0)
              continue; 
            blen -= _f_boundry.length(); 
            memmove(buf, buf+_f_boundry.length(), blen);
          }
          fprintf(stderr, "first bouundry success \n" );
        }

        while(1)
        {
          int flag = MatchBoundry(buf, blen , &boundry_pos);
          if(flag != BOUNDRY_MIDDLE )
          {
            break; 
          }

          fprintf(stderr, "middle  bouundry success \n" );
          //1,将boundry之前的数据写入文件，将数据在buf中移除
          //2，关闭文件
          //3，看boundry头是否含有文件名--雷同boundry
          WriteFile(buf,boundry_pos);
          CloseFile();
          blen -= boundry_pos;
          memmove(buf, buf + boundry_pos, blen);

          if(GetFileName(buf,&content_pos))
          {
            CreateFile();
            blen -= content_len;
            memmove(buf,buf+ content_pos, blen);
          }
          else{

            if(content_pos == 0)
              break;
            blen -= _m_boundry.length();
            memmove(buf, buf + _m_boundry.length(), blen);
          }

          
        }

        flag = MatchBoundry(buf, blen , &boundry_pos);
        if(flag == BOUNDRY_LAST)
        {
          fprintf(stderr, "last bouundry success \n" );
          
          //1将boundry之前的数据写入文件，
          //关闭文件
          //上传文件处理完毕 ，退出

          WriteFile(buf,boundry_pos );
          CloseFile();
          return true;
        }

        flag = MatchBoundry(buf, blen , &boundry_pos);
        if(flag == BOUNDRY_BAK)
        {
          //将类似boundry之前的数据写入文件
          //移除之前的数据
          //剩余的数据不动，重新继续接受数据，等补全后继续匹配
          WriteFile(buf,boundry_pos);

          blen -= (boundry_pos);
          memmove(buf, buf + boundry_pos, blen);
        }

        if(flag == BOUNDRY_NO)
        {

          //直接将buf中所有数据写入文件
          WriteFile(buf,blen);
          blen = 0;

        }

        tlen += len;
      }
      return false;

    }

};
int main()
{

/*  std::string content_len = getenv("Content-Length");
  int64_t cont_len = Utils::StrToDigit(content_len);

  int64_t tlen = 0;
  char buf[MAX_BUFF];

  while(tlen < cont_len)
  {
    int len = read(0,buf,MAX_BUFF);
    tlen += len; 
    fprintf(stderr , "content: [%s]\n",buf);
  }
  
  std::string rsp_body; 
  rsp_body = "<html><body><h1> UpLoad Success!!!</h1></body></html>";    */

  UpLoad upload; 
  std::string rsp_body; 
  if(upload.InitUpLoadInfo() == false)
  {
    return -1; 
  }

  if(upload.ProcessUpLoad() == false)
  {
    rsp_body = "<html><body><h1> UpLoad Falid!!!</h1></body></html>";               
  }
  else 
  {
    rsp_body = "<html><body><h1> UpLoad Success!!!</h1></body></html>";               
  
  }
  
  std::cout << rsp_body << std::endl;  
  fflush(stdout);
  return 0;
}
