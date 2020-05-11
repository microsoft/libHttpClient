if (NOT DEFINED STATIC_LIB_NAMES)
  message(FATAL_ERROR "STATIC_LIB_NAMES must be set")
endif()

set(SSL_PROJECT_NAME ssl)

list(APPEND STATIC_LIB_NAMES ${SSL_PROJECT_NAME})

set(PATH_TO_EXTERNAL ../../External)
set(PATH_TO_OPENSSL ${PATH_TO_EXTERNAL}/openssl)
set(PATH_TO_SSL ${PATH_TO_OPENSSL}/ssl)

set(SSL_SOURCE_FILES
    ${PATH_TO_SSL}/bio_ssl.c
    ${PATH_TO_SSL}/d1_lib.c
    ${PATH_TO_SSL}/d1_msg.c
    ${PATH_TO_SSL}/d1_srtp.c
    ${PATH_TO_SSL}/methods.c
    ${PATH_TO_SSL}/packet.c
    ${PATH_TO_SSL}/pqueue.c
    ${PATH_TO_SSL}/record/dtls1_bitmap.c
    ${PATH_TO_SSL}/record/rec_layer_d1.c
    ${PATH_TO_SSL}/record/rec_layer_s3.c
    ${PATH_TO_SSL}/record/ssl3_buffer.c
    ${PATH_TO_SSL}/record/ssl3_record.c
    ${PATH_TO_SSL}/record/ssl3_record_tls13.c
    ${PATH_TO_SSL}/s3_cbc.c
    ${PATH_TO_SSL}/s3_enc.c
    ${PATH_TO_SSL}/s3_lib.c
    ${PATH_TO_SSL}/s3_msg.c
    ${PATH_TO_SSL}/ssl_asn1.c
    ${PATH_TO_SSL}/ssl_cert.c
    ${PATH_TO_SSL}/ssl_ciph.c
    ${PATH_TO_SSL}/ssl_conf.c
    ${PATH_TO_SSL}/ssl_err.c
    ${PATH_TO_SSL}/ssl_init.c
    ${PATH_TO_SSL}/ssl_lib.c
    ${PATH_TO_SSL}/ssl_mcnf.c
    ${PATH_TO_SSL}/ssl_rsa.c
    ${PATH_TO_SSL}/ssl_sess.c
    ${PATH_TO_SSL}/ssl_stat.c
    ${PATH_TO_SSL}/ssl_txt.c
    ${PATH_TO_SSL}/ssl_utst.c
    ${PATH_TO_SSL}/statem/extensions.c
    ${PATH_TO_SSL}/statem/extensions_clnt.c
    ${PATH_TO_SSL}/statem/extensions_cust.c
    ${PATH_TO_SSL}/statem/extensions_srvr.c
    ${PATH_TO_SSL}/statem/statem.c
    ${PATH_TO_SSL}/statem/statem_clnt.c
    ${PATH_TO_SSL}/statem/statem_dtls.c
    ${PATH_TO_SSL}/statem/statem_lib.c
    ${PATH_TO_SSL}/statem/statem_srvr.c
    ${PATH_TO_SSL}/t1_enc.c
    ${PATH_TO_SSL}/t1_lib.c
    ${PATH_TO_SSL}/t1_trce.c
    ${PATH_TO_SSL}/tls13_enc.c
    ${PATH_TO_SSL}/tls_srp.c
    )

include_directories(
  ${PATH_TO_EXTERNAL}/generatedHeaders/android
  ${PATH_TO_EXTERNAL}/generatedHeaders/android/internal
  ${PATH_TO_OPENSSL}
  ${PATH_TO_SSL}/record
  ${PATH_TO_SSL}/statem
  )

add_library(${SSL_PROJECT_NAME} ${SSL_SOURCE_FILES})

target_compile_options(${SSL_PROJECT_NAME} PRIVATE -DOPENSSL_NO_DEVCRYPTOENG)
target_compile_options(${SSL_PROJECT_NAME} PRIVATE -DDSO_DLFCN)
target_compile_options(${SSL_PROJECT_NAME} PRIVATE -DHAVE_DLFCN_H)
target_compile_options(${SSL_PROJECT_NAME} PRIVATE -DNDEBUG)
target_compile_options(${SSL_PROJECT_NAME} PRIVATE -DOPENSSL_THREADS)
target_compile_options(${SSL_PROJECT_NAME} PRIVATE -DOPENSSL_NO_STATIC_ENGINE)
target_compile_options(${SSL_PROJECT_NAME} PRIVATE -D__STDC_NO_ATOMICS__)
target_compile_options(${SSL_PROJECT_NAME} PRIVATE -DOPENSSL_PIC)
target_compile_options(${SSL_PROJECT_NAME} PRIVATE -DOPENSSL_USE_NODELETE)
target_compile_options(${SSL_PROJECT_NAME} PRIVATE -DUNICODE)
