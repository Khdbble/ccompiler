FROM ubuntu:latest

RUN apt update && apt install -y gcc make binutils libc6-dev gdb sudo tzdata
# timezone setting
ENV TZ=Asia/Tokyo 
RUN apt update && apt upgrade -y && apt install git -y

RUN adduser --disabled-password --gecos '' user
RUN echo 'user ALL=(root) NOPASSWD:ALL' > /etc/sudoers.d/user
USER user
WORKDIR /home/user

# To run any command with local source (ex. run make test with local Makefile)
# docker run --rm -v $HOME/documents/ccompiler:/ccompiler -w /ccompiler compiler make test