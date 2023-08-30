
FROM debian:bookworm

RUN apt-get update 1>&2 && apt-get install -y build-essential cmake qt6-base-dev libboost-dev file 1>&2 
