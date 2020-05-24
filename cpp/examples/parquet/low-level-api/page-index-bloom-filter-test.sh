$ARROW_HOME/build/debug/parquet-writer-with-pageindex  10000000

mkdir -p $ARROW_HOME/build/debug/tests/nonmember    
mkdir -p cd $ARROW_HOME/build/debug/tests/member 

cp parquet_cpp_example_10000000_sorted.parquet member      
cp parquet_cpp_example_10000000_sorted.parquet nonmember   
cp parquet_cpp_example_10000000_unsorted.parquet member     
cp parquet_cpp_example_10000000_unsorted.parquet nonmember 

## member queries
cd $ARROW_HOME/build/debug/tests/member
$ARROW_HOME/build/debug/parquet-reader-with-pageindex parquet_cpp_example_10000000_sorted.parquet   10000000  >> debug_m_10Ms  &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex parquet_cpp_example_10000000_unsorted.parquet  10000000  >> debug_m_10Mu &



## non-member queries
cd $ARROW_HOME/build/debug/tests/nonmember
$ARROW_HOME/build/debug/parquet-reader-with-pageindex ../parquet_cpp_example_10000000_sorted.parquet   100000000  >> debug_n_10Ms &

$ARROW_HOME/build/debug/parquet-reader-with-pageindex ../parquet_cpp_example_10000000_unsorted.parquet  100000000  >> debug_n_10Mu &
