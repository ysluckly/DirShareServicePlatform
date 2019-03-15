#include "Utils.hpp"

using namespace std;

enum _boundary_type{
    BOUNDRY_NO = 0, //不是BOUNDRY
    BOUNDRY_FIRST,
    BOUNDRY_MIDDLE,
    BOUNDRY_LAST,
    BOUNDRY_BAK     //半个BOUNDRY
};

//上传文件
class Upload 
{
private:
    int _file_fd;             //文件描述符
    string _file_name;        //文件名
    string _first_boundary;     
    string _middle_boundary;
    string _last_boundary;
private:
    //不能用string,文件中可能有0
    int MatchBoundry(char* buf,size_t blen,int* boundary_pos)
    {
        //匹配字符串
        //first_boundary: ------boundary 
        //middle_boundary: \r\n------boundary\r\n
        //last_boundary: \r\n------boundary--\r\n
        //匹配上了，返回0;
        if(!memcmp(buf,_first_boundary.c_str(),_first_boundary.length())){
            *boundary_pos = 0;
            return BOUNDRY_FIRST;
        }
        for(size_t i=0; i<blen; i++){
            //防止出现半个boundary
            if((blen-i) >= _last_boundary.length()){
                if(!memcmp(buf+i,_middle_boundary.c_str(),_middle_boundary.length())){
                    *boundary_pos = i;
                    return BOUNDRY_MIDDLE;
                }
                if(!memcmp(buf+i,_last_boundary.c_str(),_last_boundary.length())){
                    *boundary_pos = i;
                    return BOUNDRY_LAST;
                }
            }
            else{
                int cmp_len = blen-i;
                if(!memcmp(buf+i,_middle_boundary.c_str(),cmp_len)){
                    *boundary_pos = i;
                    return BOUNDRY_BAK;
                }
                if(!memcmp(buf+i,_last_boundary.c_str(),cmp_len)){
                    *boundary_pos = i;
                    return BOUNDRY_BAK;
                }
            } 
        }
        return BOUNDRY_NO;
    }
    bool WriteFile(char* buf,int len)
    {
        if(_file_fd != -1){
            write(_file_fd,buf,len);
            return true;
        }
        return false;
    }
    bool GetFileName(char* buf,int* content_pos)
    {
        char* ptr = NULL;
        ptr = strstr(buf,"\r\n\r\n");
        if(ptr == NULL){
            *content_pos = 0;
            return false;
        }
        //此时content_pos是文件内容的开头
        *content_pos = ptr-buf+4;
        string header;
        header.assign(buf,ptr-buf);

        string file_sep = "filename=\"";
        size_t pos = header.find(file_sep);
        if(pos == string::npos){
            return false;
        }
        string file;
        file = header.substr(pos+file_sep.length());
        pos = file.rfind("\"");
        if(pos == string::npos){
            return false;
        }
        file.erase(pos);
        _file_name =  "www/" + file;
        fprintf(stderr,"upload file_name: %s\n",_file_name.c_str());
        return true;
    }
    bool CreateFile()
    {
        _file_fd = open(_file_name.c_str(),O_CREAT | O_WRONLY,0644);
        if(_file_fd < 0){
            cerr << "create failure!" << endl;
            return false;
        }
        return true;
    }
    bool CloseFile()
    {
        if(_file_fd != -1){
            close(_file_fd);
            _file_fd = -1;
        }
        return true;
    }
public:
    Upload():_file_fd(-1){}
    ~Upload()
    {
        close(_file_fd);
    }
    //初始化boundary信息
    bool InitUploadInfo()
    {
        umask(0);
        //文件类型
        char* ptr = getenv("Content-Type");
        if(ptr == NULL){
            cerr << "Content-Type error!" << endl;
            return false;
        }
        string boundary_sep = "boundary=";
        string content_type = ptr; 
        size_t pos = content_type.find(boundary_sep);
        if(pos == string::npos){
            cerr << "find error" << endl;
            return false;
        }
        string boundary;
        boundary = content_type.substr(pos + boundary_sep.length());
        //first_boundry后没有\r\n
        _first_boundary = "--" + boundary;
        _middle_boundary = "\r\n" + _first_boundary + "\r\n";
        _last_boundary = "\r\n" + _first_boundary + "--\r\n";
        return true; 
    }
    //对正文进行处理
    bool ProcessUpload()
    {
        // clen记录当前已经读了多少了，blen表示当前buf有多少有用字节
        int64_t clen =0,blen =0;
        //文件大小
        string content_len = getenv("Content-Length");
        int64_t flen = Utils::StrToDigit(content_len);
        cerr << flen << endl;
        //把所有的文件信息都读到 buf 里面
        char buf[MAX_BUFF] = {0};
        while(clen < flen){
            int rlen = read(0,buf+blen,MAX_BUFF);
            clen += rlen;
            blen += rlen;
            //boundary起始位置
            int boundary_pos = 0;
            //文件内容起始位置
            int content_pos = 0;
            //匹配字符串，传的是buf的长度
            int flag = MatchBoundry(buf,blen,&boundary_pos);
            if(flag == BOUNDRY_FIRST){
                //1.从boundary中获取文件名
                //2.若获取文件名成功，则创建文件，打开文件
                //3.将头信息从buf中移除，剩下的数据进行下一步匹配
                //content获取到下一个boundary的位置
                //cerr << "=========First Boundary=========" << endl; 
                //content_pos总是指向文件内容的起始位置
                if(GetFileName(buf,&content_pos)){
                    CreateFile();
                    blen -= content_pos;
                    //用剩下的内容覆盖掉前面的内容
                    memmove(buf,buf+content_pos,blen);
                    //cerr <<"buf剩余内容 : "<< endl;
                    //cerr << buf << endl;
                    //cerr << "完了" << endl;
                }
                else{
                    blen -= _first_boundary.length();
                    memmove(buf,buf+_first_boundary.length(),blen);
                }
            }
            while(1){
                int flag = MatchBoundry(buf,blen,&boundary_pos);
                //cerr << "flag: " << flag << endl;
                if(flag != BOUNDRY_MIDDLE){
                    break;
                }
                //1.匹配middle_boundary成功
                //将boundary之前的数据写入文件，将数据移除
                //看boundary头中是否有文件名
                //cerr << "=========Middle Boundary=========" << endl; 
                WriteFile(buf,boundary_pos);
                CloseFile();
                blen -= boundary_pos;
                memmove(buf,buf+boundary_pos,blen);
                if(GetFileName(buf,&content_pos)){
                    CreateFile();
                    blen -= content_pos;
                    memmove(buf,buf+content_pos,blen);
                }
                else{
                    if(content_pos == 0)
                        break;
                    blen -= _middle_boundary.length();
                    memmove(buf,buf+_middle_boundary.length(),blen);
                }
            }
            int tmp1 = MatchBoundry(buf,blen,&boundary_pos);
            if(tmp1 == BOUNDRY_LAST){
                //1.将boundary之前的数据写入文件
                //cerr << "Last Boundary! " << boundary_pos << endl;
                WriteFile(buf,boundary_pos);
                CloseFile();
                return true; 
            }
            int tmp2 = MatchBoundry(buf,blen,&boundary_pos);
            if(tmp2 == BOUNDRY_BAK){
                //将类似boundary位置之前的数据写入文件
                //移除之前的数据
                //剩下的数据不动，重新继续接收数据，补全后匹配
                //cerr << "BAK_ Boundary! " << boundary_pos << endl;
                WriteFile(buf,boundary_pos);
                blen -= boundary_pos;
                memmove(buf,buf+boundary_pos,blen);
            }
            int tmp3 = MatchBoundry(buf,blen,&boundary_pos);
            if(tmp3 == BOUNDRY_NO){
                WriteFile(buf,blen);
                blen = 0;
            }
        }
        return true;
    }
};

int main()
{
      

    Upload upload;
    string rsp_body;
    if(upload.InitUploadInfo() == false){
        rsp_body = "<html><body><h1>500<h1></body></html>\r\n";
        write(1,rsp_body.c_str(),rsp_body.length());
        return 1;
    }
    if(upload.ProcessUpload() == false){
        rsp_body = "<html><body><h1>404<h1></body></html>\r\n";
        write(1,rsp_body.c_str(),rsp_body.length());
        return 1;
    }
    else 
        rsp_body = "<html><body><h1>Upload Success!<h1></body></html>\r\n";
    write(1,rsp_body.c_str(),rsp_body.length());
    return 0;
}
