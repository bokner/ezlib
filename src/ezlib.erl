-module(ezlib).
-author("silviu.caragea").

-include("ezlib.hrl").

-export([new/1, new/2, process/2, process/3, read/1, metrics/1]).

-spec(new(Method :: integer()) ->
    {ok, SessionRef :: reference()} | badarg | {error, Reason :: binary()}).

new(Method) ->
    ezlib_nif:new_session(Method).

-spec(new(Method :: integer(), Opt :: list()) ->
    {ok, SessionRef :: reference()} | badarg | {error, Reason :: binary()}).

new(Method, Opt) ->
    ezlib_nif:new_session(Method, Opt).

-spec(process(SessionRef :: reference(), Buffer :: binary()) ->
    {ok, Data :: binary()} | badarg | {error, Reason :: binary()}).

process(SessionRef, Buffer) ->
    ezlib_nif:process_buffer(SessionRef, Buffer, true).

-spec(process(SessionRef :: reference(), Buffer :: binary(), ReturnData :: atom()) ->
    ok | {ok, Data :: binary()} | badarg | {error, Reason :: binary()}).

process(SessionRef, Buffer, ReturnData) ->
    ezlib_nif:process_buffer(SessionRef, Buffer, ReturnData).

-spec(read(SessionRef :: reference()) ->
    {ok, Data :: binary()} | badarg).

read(SessionRef) ->
    ezlib_nif:read_data(SessionRef).

-spec(metrics(SessionRef :: reference()) ->
    {ok, Data :: list()} | badarg).

metrics(SessionRef) ->
    ezlib_nif:get_stats(SessionRef).