echo Running binplace.cmd

set 
dir /s /b %TFS_OUTPUTDIRECTORY%

mkdir %TFS_SourcesDirectory%\lib
mkdir %TFS_SourcesDirectory%\lib\%TFS_PLATFORM%
copy %TFS_OUTPUTDIRECTORY%\*.lib %TFS_SourcesDirectory%\lib\%TFS_PLATFORM%

echo Done binplace.cmd
:done