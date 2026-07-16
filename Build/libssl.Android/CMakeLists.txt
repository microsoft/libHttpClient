cmake_minimum_required(VERSION 3.6)

get_filename_component(PATH_TO_ROOT "../.." ABSOLUTE)

project("libssl.Android")

set(CMAKE_STATIC_LIBRARY_PREFIX "")

###########################################
### Set up paths for source and include ###
###########################################

set(PATH_TO_EXTERNAL "${PATH_TO_ROOT}/External")
set(PATH_TO_OPENSSL "${PATH_TO_EXTERNAL}/openssl")
set(PATH_TO_SSL "${PATH_TO_OPENSSL}/ssl")

set(SSL_SOURCE_FILES
    "${PATH_TO_SSL}/bio_ssl.c"
    "${PATH_TO_SSL}/d1_lib.c"
    "${PATH_TO_SSL}/d1_msg.c"
    "${PATH_TO_SSL}/d1_srtp.c"
    "${PATH_TO_SSL}/methods.c"
    "${PATH_TO_SSL}/pqueue.c"
    "${PATH_TO_SSL}/priority_queue.c"
    "${PATH_TO_SSL}/s3_enc.c"
    "${PATH_TO_SSL}/s3_lib.c"
    "${PATH_TO_SSL}/s3_msg.c"
    "${PATH_TO_SSL}/ssl_asn1.c"
    "${PATH_TO_SSL}/ssl_cert.c"
    "${PATH_TO_SSL}/ssl_cert_comp.c"
    "${PATH_TO_SSL}/ssl_ciph.c"
    "${PATH_TO_SSL}/ssl_conf.c"
    "${PATH_TO_SSL}/ssl_err_legacy.c"
    "${PATH_TO_SSL}/ssl_init.c"
    "${PATH_TO_SSL}/ssl_lib.c"
    "${PATH_TO_SSL}/ssl_mcnf.c"
    "${PATH_TO_SSL}/ssl_rsa.c"
    "${PATH_TO_SSL}/ssl_rsa_legacy.c"
    "${PATH_TO_SSL}/ssl_sess.c"
    "${PATH_TO_SSL}/ssl_stat.c"
    "${PATH_TO_SSL}/ssl_txt.c"
    "${PATH_TO_SSL}/ssl_utst.c"
    "${PATH_TO_SSL}/t1_enc.c"
    "${PATH_TO_SSL}/t1_lib.c"
    "${PATH_TO_SSL}/t1_trce.c"
    "${PATH_TO_SSL}/tls13_enc.c"
    "${PATH_TO_SSL}/tls_depr.c"
    "${PATH_TO_SSL}/tls_srp.c"
    "${PATH_TO_SSL}/quic/cc_newreno.c"
    "${PATH_TO_SSL}/quic/json_enc.c"
    "${PATH_TO_SSL}/quic/qlog.c"
    "${PATH_TO_SSL}/quic/qlog_event_helpers.c"
    "${PATH_TO_SSL}/quic/quic_ackm.c"
    "${PATH_TO_SSL}/quic/quic_cfq.c"
    "${PATH_TO_SSL}/quic/quic_channel.c"
    "${PATH_TO_SSL}/quic/quic_demux.c"
    "${PATH_TO_SSL}/quic/quic_engine.c"
    "${PATH_TO_SSL}/quic/quic_fc.c"
    "${PATH_TO_SSL}/quic/quic_fifd.c"
    "${PATH_TO_SSL}/quic/quic_impl.c"
    "${PATH_TO_SSL}/quic/quic_lcidm.c"
    "${PATH_TO_SSL}/quic/quic_method.c"
    "${PATH_TO_SSL}/quic/quic_obj.c"
    "${PATH_TO_SSL}/quic/quic_port.c"
    "${PATH_TO_SSL}/quic/quic_rcidm.c"
    "${PATH_TO_SSL}/quic/quic_reactor.c"
    "${PATH_TO_SSL}/quic/quic_reactor_wait_ctx.c"
    "${PATH_TO_SSL}/quic/quic_record_rx.c"
    "${PATH_TO_SSL}/quic/quic_record_shared.c"
    "${PATH_TO_SSL}/quic/quic_record_tx.c"
    "${PATH_TO_SSL}/quic/quic_record_util.c"
    "${PATH_TO_SSL}/quic/quic_rstream.c"
    "${PATH_TO_SSL}/quic/quic_rx_depack.c"
    "${PATH_TO_SSL}/quic/quic_sf_list.c"
    "${PATH_TO_SSL}/quic/quic_srt_gen.c"
    "${PATH_TO_SSL}/quic/quic_srtm.c"
    "${PATH_TO_SSL}/quic/quic_sstream.c"
    "${PATH_TO_SSL}/quic/quic_statm.c"
    "${PATH_TO_SSL}/quic/quic_stream_map.c"
    "${PATH_TO_SSL}/quic/quic_thread_assist.c"
    "${PATH_TO_SSL}/quic/quic_tls.c"
    "${PATH_TO_SSL}/quic/quic_tls_api.c"
    "${PATH_TO_SSL}/quic/quic_trace.c"
    "${PATH_TO_SSL}/quic/quic_tserver.c"
    "${PATH_TO_SSL}/quic/quic_txp.c"
    "${PATH_TO_SSL}/quic/quic_txpim.c"
    "${PATH_TO_SSL}/quic/quic_types.c"
    "${PATH_TO_SSL}/quic/quic_wire.c"
    "${PATH_TO_SSL}/quic/quic_wire_pkt.c"
    "${PATH_TO_SSL}/quic/uint_set.c"
    "${PATH_TO_SSL}/record/rec_layer_d1.c"
    "${PATH_TO_SSL}/record/rec_layer_s3.c"
    "${PATH_TO_SSL}/record/methods/dtls_meth.c"
    "${PATH_TO_SSL}/record/methods/ssl3_meth.c"
    "${PATH_TO_SSL}/record/methods/tls13_meth.c"
    "${PATH_TO_SSL}/record/methods/tls1_meth.c"
    "${PATH_TO_SSL}/record/methods/tls_common.c"
    "${PATH_TO_SSL}/record/methods/tls_multib.c"
    "${PATH_TO_SSL}/record/methods/tlsany_meth.c"
    "${PATH_TO_SSL}/rio/poll_builder.c"
    "${PATH_TO_SSL}/rio/poll_immediate.c"
    "${PATH_TO_SSL}/rio/rio_notifier.c"
    "${PATH_TO_SSL}/statem/extensions.c"
    "${PATH_TO_SSL}/statem/extensions_clnt.c"
    "${PATH_TO_SSL}/statem/extensions_cust.c"
    "${PATH_TO_SSL}/statem/extensions_srvr.c"
    "${PATH_TO_SSL}/statem/statem.c"
    "${PATH_TO_SSL}/statem/statem_clnt.c"
    "${PATH_TO_SSL}/statem/statem_dtls.c"
    "${PATH_TO_SSL}/statem/statem_lib.c"
    "${PATH_TO_SSL}/statem/statem_srvr.c"
    )

set(SSL_INCLUDE_DIRS
    "${PATH_TO_EXTERNAL}/opensslGeneratedHeaders/android"
    "${PATH_TO_EXTERNAL}/opensslGeneratedHeaders/android/internal"
    "${PATH_TO_OPENSSL}"
    "${PATH_TO_OPENSSL}/include"
    "${PATH_TO_SSL}/record"
    "${PATH_TO_SSL}/statem"
    )

#########################
### Set up static lib ###
#########################

add_library(
    "${PROJECT_NAME}"
    STATIC
    "${SSL_SOURCE_FILES}"
)

target_include_directories(
    "${PROJECT_NAME}"
    PRIVATE
    "${SSL_INCLUDE_DIRS}"
)

include("../libHttpClient.CMake/GetOpenSSLFlags.cmake")
get_openssl_flags(FLAGS FLAGS_DEBUG FLAGS_RELEASE)

include("../libHttpClient.CMake/TargetSetFlags.cmake")
target_set_flags(
    "${PROJECT_NAME}"
    "${FLAGS}"
    "${FLAGS_DEBUG}"
    "${FLAGS_RELEASE}"
)
