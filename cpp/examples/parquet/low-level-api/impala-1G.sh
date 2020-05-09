source $IMPALA_HOME/bin/impala-config.sh
$HADOOP_HOME/bin/hdfs dfs -rm -r -skipTrash /panos/import_dir/UNCOMPRESSED

$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/import_dir/UNCOMPRESSED/1Gfiles/1M/
$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/import_dir/UNCOMPRESSED/1Gfiles/1pt15M/
$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/import_dir/UNCOMPRESSED/1Gfiles/1pt4M/
$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/import_dir/UNCOMPRESSED/1Gfiles/1pt75M/
$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/import_dir/UNCOMPRESSED/1Gfiles/2pt3M/
$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/import_dir/UNCOMPRESSED/1Gfiles/3pt5M/
$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/import_dir/UNCOMPRESSED/1Gfiles/7M/

$HADOOP_HOME/bin/hdfs dfs -copyFromLocal ~/parquet_data/testing/imports/UNCOMPRESSED/1g_files/parquet_cpp_example_1Gfile_1000000.parquet /panos/import_dir/UNCOMPRESSED/1Gfiles/1M/1G-1M.parquet
$HADOOP_HOME/bin/hdfs dfs -copyFromLocal ~/parquet_data/testing/imports/UNCOMPRESSED/1g_files/parquet_cpp_example_1Gfile_1150000.parquet /panos/import_dir/UNCOMPRESSED/1Gfiles/1pt15M/1G-1pt15M.parquet
$HADOOP_HOME/bin/hdfs dfs -copyFromLocal ~/parquet_data/testing/imports/UNCOMPRESSED/1g_files/parquet_cpp_example_1Gfile_1400000.parquet /panos/import_dir/UNCOMPRESSED/1Gfiles/1pt4M/1G-1pt4M.parquet
$HADOOP_HOME/bin/hdfs dfs -copyFromLocal ~/parquet_data/testing/imports/UNCOMPRESSED/1g_files/parquet_cpp_example_1Gfile_1750000.parquet /panos/import_dir/UNCOMPRESSED/1Gfiles/1pt75M/1G-1pt75M.parquet
$HADOOP_HOME/bin/hdfs dfs -copyFromLocal ~/parquet_data/testing/imports/UNCOMPRESSED/1g_files/parquet_cpp_example_1Gfile_2300000.parquet /panos/import_dir/UNCOMPRESSED/1Gfiles/2pt3M/1G-2pt3M.parquet
$HADOOP_HOME/bin/hdfs dfs -copyFromLocal ~/parquet_data/testing/imports/UNCOMPRESSED/1g_files/parquet_cpp_example_1Gfile_3500000.parquet /panos/import_dir/UNCOMPRESSED/1Gfiles/3pt5M/1G-3pt5M.parquet
$HADOOP_HOME/bin/hdfs dfs -copyFromLocal ~/parquet_data/testing/imports/UNCOMPRESSED/1g_files/parquet_cpp_example_1Gfile_7000000.parquet /panos/import_dir/UNCOMPRESSED/1Gfiles/7M/1G-7M.parquet

$HADOOP_HOME/bin/hdfs dfs -ls -R /panos/import_dir/UNCOMPRESSED

$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE all_types_table_u_1M LIKE PARQUET '/panos/import_dir/UNCOMPRESSED/1Gfiles/1M/1G-1M.parquet' STORED AS PARQUET LOCATION '/panos/import_dir/UNCOMPRESSED/1Gfiles/1M';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE all_types_table_u_1pt15M LIKE PARQUET '/panos/import_dir/UNCOMPRESSED/1Gfiles/1pt15M/1G-1pt15M.parquet' STORED AS PARQUET LOCATION '/panos/import_dir/UNCOMPRESSED/1Gfiles/1pt15M/';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE all_types_table_u_1pt4M LIKE PARQUET '/panos/import_dir/UNCOMPRESSED/1Gfiles/1pt4M/1G-1pt4M.parquet' STORED AS PARQUET LOCATION '/panos/import_dir/UNCOMPRESSED/1Gfiles/1pt4M/';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE all_types_table_u_1pt75M LIKE PARQUET '/panos/import_dir/UNCOMPRESSED/1Gfiles/1pt75M/1G-1pt75M.parquet' STORED AS PARQUET LOCATION '/panos/import_dir/UNCOMPRESSED/1Gfiles/1pt75M/';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE all_types_table_u_2pt3M LIKE PARQUET '/panos/import_dir/UNCOMPRESSED/1Gfiles/2pt3M/1G-2pt3M.parquet' STORED AS PARQUET LOCATION '/panos/import_dir/UNCOMPRESSED/1Gfiles/2pt3M/';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE all_types_table_u_3pt5M LIKE PARQUET '/panos/import_dir/UNCOMPRESSED/1Gfiles/3pt5M/1G-3pt5M.parquet' STORED AS PARQUET LOCATION '/panos/import_dir/UNCOMPRESSED/1Gfiles/3pt5M/';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE all_types_table_u_7M LIKE PARQUET '/panos/import_dir/UNCOMPRESSED/1Gfiles/7M/1G-7M.parquet' STORED AS PARQUET LOCATION '/panos/import_dir/UNCOMPRESSED/1Gfiles/7M/';"

$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from all_types_table_u_1M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from all_types_table_u_1pt15M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from all_types_table_u_1pt4M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from all_types_table_u_1pt75M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from all_types_table_u_2pt3M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from all_types_table_u_3pt5M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from all_types_table_u_7M;"

$HADOOP_HOME/bin/hdfs dfs -rm -r -skipTrash /panos/export_dir/UNCOMPRESSED/
$HADOOP_HOME/bin/hdfs dfs -mkdir -p /panos/export_dir/UNCOMPRESSED/1Gfiles/

$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE export_all_types_table_u_1M LIKE all_types_table_u_1M STORED AS PARQUET LOCATION '/panos/export_dir/UNCOMPRESSED/1Gfiles/1M';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE export_all_types_table_u_1pt15M LIKE all_types_table_u_1pt15M STORED AS PARQUET LOCATION '/panos/export_dir/UNCOMPRESSED/1Gfiles/1pt15M/';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE export_all_types_table_u_1pt4M LIKE all_types_table_u_1pt4M STORED AS PARQUET LOCATION '/panos/export_dir/UNCOMPRESSED/1Gfiles/1pt4M/';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE export_all_types_table_u_1pt75M LIKE all_types_table_u_1pt75M STORED AS PARQUET LOCATION '/panos/export_dir/UNCOMPRESSED/1Gfiles/1pt75M/';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE export_all_types_table_u_2pt3M LIKE all_types_table_u_2pt3M STORED AS PARQUET LOCATION '/panos/export_dir/UNCOMPRESSED/1Gfiles/2pt3M/';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE export_all_types_table_u_3pt5M LIKE all_types_table_u_3pt5M STORED AS PARQUET LOCATION '/panos/export_dir/UNCOMPRESSED/1Gfiles/3pt5M/';"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;CREATE EXTERNAL TABLE export_all_types_table_u_7M LIKE all_types_table_u_7M STORED AS PARQUET LOCATION '/panos/export_dir/UNCOMPRESSED/1Gfiles/7M/';"

$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;INSERT INTO TABLE  export_all_types_table_u_1M SELECT * FROM all_types_table_u_1M;"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;INSERT INTO TABLE  export_all_types_table_u_1pt15M SELECT * FROM all_types_table_u_1pt15M;"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;INSERT INTO TABLE  export_all_types_table_u_1pt4M SELECT * FROM all_types_table_u_1pt4M;"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;INSERT INTO TABLE  export_all_types_table_u_1pt75M SELECT * FROM all_types_table_u_1pt75M;"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;INSERT INTO TABLE  export_all_types_table_u_2pt3M SELECT * FROM all_types_table_u_2pt3M;"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;INSERT INTO TABLE  export_all_types_table_u_3pt5M SELECT * FROM all_types_table_u_3pt5M;"
$IMPALA_HOME/bin/impala-shell.sh -q "SET COMPRESSION_CODEC=NONE;set PARQUET_FILE_SIZE=1g;INSERT INTO TABLE  export_all_types_table_u_7M SELECT * FROM all_types_table_u_7M;"

$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from export_all_types_table_u_1M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from export_all_types_table_u_1pt15M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from export_all_types_table_u_1pt4M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from export_all_types_table_u_1pt75M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from export_all_types_table_u_2pt3M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from export_all_types_table_u_3pt5M;"
$IMPALA_HOME/bin/impala-shell.sh -q "select count(*) from export_all_types_table_u_7M;"

mkdir -p ~/parquet_data/testing/exports/UNCOMPRESSED/1g_files/

hadoop fs -get /panos/export_dir/UNCOMPRESSED/1Gfiles/*   ~/parquet_data/testing/exports/UNCOMPRESSED/1g_files/

$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_1M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_1pt15M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_1pt4M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_1pt75M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_2pt3M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_3pt5M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE all_types_table_u_7M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_1M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_1pt15M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_1pt4M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_1pt75M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_2pt3M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_3pt5M";
$IMPALA_HOME/bin/impala-shell.sh -q "DROP TABLE export_all_types_table_u_7M";

$HADOOP_HOME/bin/hdfs dfs -rm -r -skipTrash /panos/import_dir/UNCOMPRESSED
$HADOOP_HOME/bin/hdfs dfs -rm -r -skipTrash /panos/export_dir/UNCOMPRESSED

ls -laR ~/parquet_data/testing/exports/UNCOMPRESSED/1g_files/