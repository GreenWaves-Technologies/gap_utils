
if [ $# -eq 0 ]
  then
    echo "Run the trim test with fuse"
    fuse=1
else
    echo "Run the trim test w/o fuse"
    fuse=0
fi

make clean all run
make clean all run RUN_125C_TEST=0 FUSE_TRIM_VAL=$fuse

