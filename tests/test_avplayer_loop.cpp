// Exercise the production demuxer with real FFmpeg and synthetic media.
// Queues consume packets immediately; decoder threads and callbacks are fixtures.
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>
extern "C" {
#include <libavformat/avformat.h>
}

static bool force_seek_error,force_empty_after_seek,after_seek;
static unsigned seek_calls;
static int test_seek(AVFormatContext* c,int stream,int64_t min,int64_t target,int64_t max,int flags) {
    ++seek_calls;
    if(force_seek_error)return AVERROR(EIO);
    int result=avformat_seek_file(c,stream,min,target,max,flags);
    if(result>=0)after_seek=true;
    return result;
}
static int test_read(AVFormatContext* c,AVPacket* packet) {
    return force_empty_after_seek && after_seek ? AVERROR_EOF : av_read_frame(c,packet);
}
namespace Common { static void SetCurrentThreadName(const char*) {} }
#define LOG_INFO(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define ORBIS_AVPLAYER_ERROR_WAR_LOOPING_BACK 1
#define avformat_seek_file test_seek
#define av_read_frame test_read

class AvPlayerSource;
static std::stop_source stop_source;
struct State {
    unsigned warnings{},errors{},eof{},total{},video{},audio{},previous{},previous_video{},previous_audio{};
    std::vector<unsigned> loops,video_loops,audio_loops;
    void OnError(){++errors;}
    void OnEOF(){++eof;}
    void OnWarning(int) {
        ++warnings;loops.push_back(total-previous);video_loops.push_back(video-previous_video);
        audio_loops.push_back(audio-previous_audio);
        previous=total;previous_video=video;previous_audio=audio;
        if(warnings==3)stop_source.request_stop();
    }
};
struct Signal {void Notify(){};void Join(){};};
class AvPlayerSource {
public:
    static void ReleaseAVPacket(AVPacket* packet){av_packet_free(&packet);}
    using AVPacketPtr=std::unique_ptr<AVPacket,void(*)(AVPacket*)>;
    struct Queue {
        State* state{};bool video{};
        size_t Size()const{return 0;}
        void Push(AVPacketPtr packet){assert(packet);++state->total;if(video)++state->video;else ++state->audio;}
    };
    struct Context {AVFormatContext* pointer;AVFormatContext* get(){return pointer;}
        AVFormatContext* operator->(){return pointer;}} m_avformat_context;
    struct Stream {int ffmpeg_index;};
    State& m_state;
    Queue m_video_packets,m_audio_packets;
    Signal m_video_packets_cv,m_audio_packets_cv,m_video_frames_cv,m_audio_frames_cv;
    Signal m_video_buffers_cv,m_audio_buffers_cv,m_video_decoder_thread,m_audio_decoder_thread,m_demuxer_thread;
    std::optional<int> m_video_stream_index,m_audio_stream_index;
    std::vector<Stream> m_streams;
    bool m_is_looping=true,m_is_eof=false;
    AvPlayerSource(State& s,AVFormatContext* c):m_avformat_context{c},m_state(s),
        m_video_packets{&s,true},m_audio_packets{&s,false} {
        for(unsigned i=0;i<c->nb_streams;++i){
            m_streams.push_back({static_cast<int>(i)});
            if(c->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO)m_video_stream_index=i;
            if(c->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO)m_audio_stream_index=i;
        }
    }
    void DemuxerThread(std::stop_token stop);
};

// Generated from the actual production translation unit by the Python runner.
#include "avplayer_demuxer.inc"

// Callback-backed input exercises FFmpeg's custom AVIO path, used by the game.
struct FileInput {std::FILE* file;long size;};
static int read_file(void* opaque,uint8_t* buffer,int count){
    auto& input=*static_cast<FileInput*>(opaque);
    int got=static_cast<int>(std::fread(buffer,1,count,input.file));return got ? got : AVERROR_EOF;
}
static int64_t seek_file(void* opaque,int64_t offset,int whence){
    auto& input=*static_cast<FileInput*>(opaque);
    if(whence & AVSEEK_SIZE)return input.size;
    if(whence!=SEEK_SET && whence!=SEEK_CUR && whence!=SEEK_END)return -1;
    if(std::fseek(input.file,static_cast<long>(offset),whence))return -1;
    return std::ftell(input.file);
}

int main(int argc,char** argv){
    assert(argc==3 || argc==4);const int mode=std::atoi(argv[2]);
    force_seek_error=mode==1;force_empty_after_seek=mode==2;
    AVFormatContext* context=nullptr;
    AVIOContext* io=nullptr;FileInput input{};
    if(argc==4){
        input.file=std::fopen(argv[1],"rb");assert(input.file);
        assert(!std::fseek(input.file,0,SEEK_END));input.size=std::ftell(input.file);std::rewind(input.file);
        io=avio_alloc_context(static_cast<unsigned char*>(av_malloc(4096)),4096,0,&input,
                              read_file,nullptr,seek_file);assert(io);
        context=avformat_alloc_context();assert(context);context->pb=io;context->flags|=AVFMT_FLAG_CUSTOM_IO;
    }
    assert(avformat_open_input(&context,argc==4 ? nullptr : argv[1],nullptr,nullptr)>=0);
    assert(avformat_find_stream_info(context,nullptr)>=0);
    State state;AvPlayerSource source(state,context);source.m_is_looping=mode!=3;
    source.DemuxerThread(stop_source.get_token());
    if(mode==0){
        assert(state.errors==0 && state.warnings==3 && seek_calls==3);
        assert(state.loops.size()==3 && state.loops[0]>0 && state.loops[1]>0 && state.loops[2]>0);
        assert(state.loops[1]==state.loops[2]);
        assert(state.video_loops[1]==state.video_loops[2]);
        assert(state.audio_loops[1]==state.audio_loops[2]);
        std::printf("PASS loops=%u,%u,%u video=%u,%u,%u audio=%u,%u,%u\n",
            state.loops[0],state.loops[1],state.loops[2],state.video_loops[0],state.video_loops[1],
            state.video_loops[2],state.audio_loops[0],state.audio_loops[1],state.audio_loops[2]);
    }else if(mode==1){
        assert(state.errors==1 && state.warnings==0 && seek_calls==1);std::puts("PASS seek failure reported once");
    }else if(mode==2){
        assert(state.errors==1 && state.warnings==1 && seek_calls==1);std::puts("PASS empty restart bounded");
    }else{
        assert(state.errors==0 && state.warnings==0 && seek_calls==0 && state.eof==1);
        std::puts("PASS non-loop EOF preserved");
    }
    avformat_close_input(&context);
    if(io){av_freep(&io->buffer);avio_context_free(&io);std::fclose(input.file);}
    return 0;
}
