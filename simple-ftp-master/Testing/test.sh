#!/bin/bash 
FTP_PATH="../.."

cd example_dir
$FTP_PATH/siftp localhost 77777 < commands1.txt &
cd ..

cd example_dir2
$FTP_PATH/siftp  localhost 77777 < ./commands2.txt &
cd ..
