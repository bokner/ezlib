#include "ezlib.h"
#include "bytebuffer.h"
#include "ezlib_nif.h"
#include "nif_utils.h"
#include "macros.h"

#include <zlib.h>

#include <string>

#define USE_STATS

#if defined(USE_STATS)
    #define UINT64_METRIC(Name, Property) enif_make_tuple2(env, make_atom(env, Name), enif_make_uint64(env, Property))
    #define DOUBLE_METRIC(Name, Property) enif_make_tuple2(env, make_atom(env, Name), enif_make_double(env, Property))
#endif

#define DEFLATE 1
#define INFLATE 2
#define CHUNK_SIZE 2048

#define DEFAULT_BUFFER_CAPACITY 1024
#define MAX_BUFFER_CAPACITY 16000

#if defined(USE_STATS)
struct CompressionStat
{
    CompressionStat() : raw_bytes(0), processed_bytes(0) {}
    
    size_t raw_bytes;
    size_t processed_bytes;
};
#endif

typedef int (*PROCESSING_FUNCTION)(z_stream* strm, int flush);

struct zlib_session
{
    ByteBuffer* buffer;
    z_stream*  stream;
    int method;
    PROCESSING_FUNCTION processing_function;
#if defined(USE_STATS)
    CompressionStat* stats;
#endif
};

z_stream* create_stream()
{
    z_stream* stream = static_cast<z_stream*>(enif_alloc(sizeof(z_stream)));
    
    if(!stream)
        return NULL;
    
    stream->next_in = NULL;
    stream->avail_in = 0;
    stream->total_in = 0;
    stream->next_out = NULL;
    stream->avail_out = 0;
    stream->total_out = 0;
    stream->msg = NULL;
    stream->state = NULL;
    stream->zalloc = Z_NULL;
    stream->zfree = Z_NULL;
    stream->opaque = Z_NULL;
    stream->data_type = Z_BINARY;
    stream->adler = 0;
    stream->reserved = 0;
    
    return stream;
}

ZEXTERN int ZEXPORT deflate OF((z_streamp strm, int flush));

bool process_buffer(zlib_session* session, unsigned char* data, size_t len)
{
#if defined(USE_STATS)
    session->stats->raw_bytes += len;
#endif

    int result;
    size_t bytes_to_write;
    unsigned char chunk[CHUNK_SIZE];
    
    session->stream->avail_in = len;
    session->stream->next_in = data;
    
    do
    {
        session->stream->avail_out = CHUNK_SIZE;
        session->stream->next_out =  chunk;
        
        result = session->processing_function(session->stream, Z_NO_FLUSH);
        
        if (result != Z_OK)
            return false;
        
        bytes_to_write = CHUNK_SIZE - session->stream->avail_out;
        
        if(bytes_to_write > 0)
        {
#if defined(USE_STATS)
            session->stats->processed_bytes += bytes_to_write;
#endif
            session->buffer->WriteBytes(reinterpret_cast<const char*>(chunk), bytes_to_write);
        }
    }
    while (session->stream->avail_out == 0);
    
    if(session->method == DEFLATE)
    {
        //Flush data
        
        do
        {
            session->stream->avail_out = CHUNK_SIZE;
            session->stream->next_out = chunk;
            result = session->processing_function(session->stream, Z_SYNC_FLUSH);
            
            if (result != Z_OK)
                return false;
            
            bytes_to_write = CHUNK_SIZE - session->stream->avail_out;
            
            if(bytes_to_write > 0)
            {
    #if defined(USE_STATS)
                session->stats->processed_bytes += bytes_to_write;
    #endif
                session->buffer->WriteBytes(reinterpret_cast<const char*>(chunk), bytes_to_write);
            }
        }
        while (session->stream->avail_out == 0);
    }
    
    return true;
}

void nif_zlib_session_free(ErlNifEnv* env, void* obj)
{
    UNUSED(env);
    
    zlib_session *session = static_cast<zlib_session*>(obj);
    
    if(session->stream)
    {
        if(session->method == DEFLATE)
            deflateEnd(session->stream);
        else
            inflateEnd(session->stream);
        
        enif_free(session->stream);
    }
    
    if(session->buffer)
        delete session->buffer;
    
#if defined(USE_STATS)
    if(session->stats)
        delete session->stats;
#endif
    
}

ERL_NIF_TERM nif_zlib_new_session(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ezlib_data* data = static_cast<ezlib_data*>(enif_priv_data(env));
    
    int method;
    
    if(!enif_get_int(env, argv[0], &method))
        return enif_make_badarg(env);
    
    if(method != DEFLATE && method != INFLATE)
        return enif_make_badarg(env);

    int compression_level = Z_DEFAULT_COMPRESSION;
    int window_bits = 15;
    int mem_level = 8;
    int compression_strategy = Z_DEFAULT_STRATEGY;
    
    if(argc == 2)
    {
        if(!enif_is_list(env, argv[1]))
            return enif_make_badarg(env);
    
        const ERL_NIF_TERM *items;
        int arity;
        
        ERL_NIF_TERM settings_list = argv[1];
        ERL_NIF_TERM head;
    
        while(enif_get_list_cell(env, settings_list, &head, &settings_list))
        {
            if(!enif_get_tuple(env, head, &arity, &items) || arity != 2)
                return enif_make_badarg(env);
            
            if(enif_is_identical(items[0], ATOMS.atomCompressionLevel))
            {
                int value;
                
                if(!enif_get_int(env, items[1], &value))
                    return enif_make_badarg(env);
                
                compression_level = value;
            }
            else if(enif_is_identical(items[0], ATOMS.atomWindowBits))
            {
                int value;
                
                if(!enif_get_int(env, items[1], &value))
                    return enif_make_badarg(env);
                
                window_bits = value;
            }
            else if(enif_is_identical(items[0], ATOMS.atomMemLevel))
            {
                int value;
                
                if(!enif_get_int(env, items[1], &value))
                    return enif_make_badarg(env);
                
                mem_level = value;
            }
            else if(enif_is_identical(items[0], ATOMS.atomCompStrategy))
            {
                int value;
                
                if(!enif_get_int(env, items[1], &value))
                    return enif_make_badarg(env);
                
                compression_strategy = value;
            }
            else
            {
                return enif_make_badarg(env);
            }
        }
    }
    
    z_stream* stream = create_stream();
    
    if(stream == NULL)
        return make_error(env, "create_stream failed");
    
    if(method == DEFLATE)
    {
        if(deflateInit2(stream, compression_level, Z_DEFLATED, window_bits, mem_level, compression_strategy) != Z_OK)
        {
            enif_free(stream);
            return make_error(env, "deflateInit failed");
        }
    }
    else
    {
        if(inflateInit2(stream, window_bits) != Z_OK)
        {
            enif_free(stream);
            return make_error(env, "inflateInit failed");
        }
    }
    
    zlib_session* session = static_cast<zlib_session*>(enif_alloc_resource(data->resZlibSession, sizeof(zlib_session)));
    
    if(session == NULL)
    {
        if(method == DEFLATE)
            deflateEnd(stream);
        else
            inflateEnd(stream);
   
        enif_free(stream);
        return make_error(env, "enif_alloc_resource failed");
    }
    
    session->buffer = new ByteBuffer(DEFAULT_BUFFER_CAPACITY);
#if defined(USE_STATS)
    session->stats = new CompressionStat();
#endif
    session->method = method;
    session->stream = stream;
    session->processing_function = (method == DEFLATE ? deflate : inflate);
    
    ERL_NIF_TERM term = enif_make_resource(env, session);
    enif_release_resource(session);
    
    return enif_make_tuple2(env, ATOMS.atomOk, term);
}

ERL_NIF_TERM nif_zlib_process_buffer(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    UNUSED(argc);
    
    ezlib_data* data = static_cast<ezlib_data*>(enif_priv_data(env));
    
    zlib_session* session = NULL;
    
    if(!enif_get_resource(env, argv[0], data->resZlibSession, (void**) &session))
        return enif_make_badarg(env);
    
    ErlNifBinary in_buffer;
        
    if(!enif_inspect_binary(env, argv[1], &in_buffer))
        return enif_make_badarg(env);
    
    bool return_data = enif_is_identical(ATOMS.atomTrue, argv[2]);
    
    if(!process_buffer(session, in_buffer.data, in_buffer.size))
    {
        std::string error("process_buffer failed: ");
        
        if(session->stream->msg)
            error.append(session->stream->msg);
        
        return make_error(env, error.c_str());
    }
    
    if(!return_data)
        return ATOMS.atomOk;
    
    size_t length = session->buffer->Length();
    ERL_NIF_TERM term = make_binary(env, session->buffer->Data(), length);
    session->buffer->Consume(length);
    
    if(session->buffer->Capacity() > MAX_BUFFER_CAPACITY)
        session->buffer->Resize(DEFAULT_BUFFER_CAPACITY);
    
    return enif_make_tuple2(env, ATOMS.atomOk, term);
}

ERL_NIF_TERM nif_zlib_read_data(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    UNUSED(argc);
    
    ezlib_data* data = static_cast<ezlib_data*>(enif_priv_data(env));
    
    zlib_session* session = NULL;
    
    if(!enif_get_resource(env, argv[0], data->resZlibSession, (void**) &session))
        return enif_make_badarg(env);
    
    size_t length = session->buffer->Length();
    ERL_NIF_TERM term = make_binary(env, session->buffer->Data(), length);
    session->buffer->Consume(length);
    
    if(session->buffer->Capacity() > MAX_BUFFER_CAPACITY)
        session->buffer->Resize(DEFAULT_BUFFER_CAPACITY);
    
    return enif_make_tuple2(env, ATOMS.atomOk, term);
}

ERL_NIF_TERM nif_get_stats(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    UNUSED(argc);
    
    ezlib_data* data = static_cast<ezlib_data*>(enif_priv_data(env));
    
    zlib_session* session = NULL;
    
    if(!enif_get_resource(env, argv[0], data->resZlibSession, (void**) &session))
        return enif_make_badarg(env);
    
#if defined(USE_STATS)
    CompressionStat stat = *session->stats;
    double ratio;
    
    if(session->method == DEFLATE)
        ratio = (1.0f- ((double)stat.processed_bytes/(double)stat.raw_bytes))*100;
    else
        ratio = (1.0f- ((double)stat.raw_bytes/(double)stat.processed_bytes))*100;
       
    ERL_NIF_TERM stats = enif_make_tuple(env, 3, UINT64_METRIC("raw_bytes", session->stats->raw_bytes),
                                                 UINT64_METRIC("processed_bytes", session->stats->processed_bytes),
                                                 DOUBLE_METRIC("ratio", ratio));
    
    return enif_make_tuple2(env, ATOMS.atomOk, stats);
#else
    return make_error(env, "Not available. Please compile with USE_STATS");
#endif
}


