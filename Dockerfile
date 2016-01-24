FROM fedora:21

RUN yum -y groupinstall "Development Tools"
RUN yum -y install git autoconf automake glib2-devel

ADD . /src/ps2emu

RUN cd /src/ps2emu &&\
    ./autogen.sh &&\
    ./configure &&\
    make &&\
    make install
