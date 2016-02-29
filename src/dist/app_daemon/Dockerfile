FROM rdsn-dev

COPY daemon /home/rdsn/

COPY config.ini /home/rdsn/

ENV LD_LIBRARY_PATH /home/rdsn/lib

CMD ["/home/rdsn/daemon", "/home/rdsn/config.ini" ]

