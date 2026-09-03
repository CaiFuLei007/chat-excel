#!/bin/bash

envsubst < ${CONF_NAME} > chat_data.conf

exec ./${BIN_NAME}