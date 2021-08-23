cmake_minimum_required(VERSION 3.6)

get_filename_component(PATH_TO_ROOT "../../../../.." ABSOLUTE)

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
    "${PATH_TO_SSL}/packet.c"
    "${PATH_TO_SSL}/pqueue.c"
    "${PATH_TO_SSL}/record/dtls1_bitmap.c"
    "${PATH_TO_SSL}/record/rec_layer_d1.c"
    "${PATH_TO_SSL}/record/rec_layer_s3.c"
    "${PATH_TO_SSL}/record/ssl3_buffer.c"
    "${PATH_TO_SSL}/record/ssl3_record.c"
    "${PATH_TO_SSL}/record/ssl3_record_tls13.c"
    "${PATH_TO_SSL}/s3_cbc.c"
    "${PATH_TO_SSL}/s3_enc.c"
    "${PATH_TO_SSL}/s3_lib.c"
    "${PATH_TO_SSL}/s3_msg.c"
    "${PATH_TO_SSL}/ssl_asn1.c"
    "${PATH_TO_SSL}/ssl_cert.c"
    "${PATH_TO_SSL}/ssl_ciph.c"
    "${PATH_TO_SSL}/ssl_conf.c"
    "${PATH_TO_SSL}/ssl_err.c"
    "${PATH_TO_SSL}/ssl_init.c"
    "${PATH_TO_SSL}/ssl_lib.c"
    "${PATH_TO_SSL}/ssl_mcnf.c"
    "${PATH_TO_SSL}/ssl_rsa.c"
    "${PATH_TO_SSL}/ssl_sess.c"
    "${PATH_TO_SSL}/ssl_stat.c"
    "${PATH_TO_SSL}/ssl_txt.c"
    "${PATH_TO_SSL}/ssl_utst.c"
    "${PATH_TO_SSL}/statem/extensions.c"
    "${PATH_TO_SSL}/statem/extensions_clnt.c"
    "${PATH_TO_SSL}/statem/extensions_cust.c"
    "${PATH_TO_SSL}/statem/extensions_srvr.c"
    "${PATH_TO_SSL}/statem/statem.c"
    "${PATH_TO_SSL}/statem/statem_clnt.c"
    "${PATH_TO_SSL}/statem/statem_dtls.c"
    "${PATH_TO_SSL}/statem/statem_lib.c"
    "${PATH_TO_SSL}/statem/statem_srvr.c"
    "${PATH_TO_SSL}/t1_enc.c"
    "${PATH_TO_SSL}/t1_lib.c"
    "${PATH_TO_SSL}/t1_trce.c"
    "${PATH_TO_SSL}/tls13_enc.c"
    "${PATH_TO_SSL}/tls_srp.c"
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

include("../GetOpenSSLFlags.cmake")
get_openssl_flags(FLAGS FLAGS_DEBUG FLAGS_RELEASE)

include("../../TargetSetFlags.cmake")
target_set_flags(
    "${PROJECT_NAME}"
    "${FLAGS}"
    "${FLAGS_DEBUG}"
    "${FLAGS_RELEASE}"
)
