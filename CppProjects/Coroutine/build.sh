#!/bin/sh
BuildDir="Build"
OutputDir="Output"

PrintHelp()
{
	if [ $# -eq 0 ]; then
		printf "Possible commands:\n"
		printf "\tbuild\n"
		printf "\tclean\n"
		printf "\thelp\n"
	else
		case $1 in
			"build")
				echo "Buildings a project. Need to set type of building (debug or release)"
				;;
			"clean")
				echo "Cleans a project"
				;;
			"help")
				echo "Shows the help"
				;;
			*)
				echo "This function is not exist"
				;;
		esac
	fi
}
if [ $# -eq 0 ]; then
	PrintHelp
elif [ $1 = "build" ]; then
	BuildType=''
	if [ $# -lt 2 ]; then
		echo "Build type does not set!"
		exit 2
	else
		case $2 in
			"debug" | "Debug")
				BuildType="Debug"
				;;
			"release" | "Release")
				BuildType="Release"
				;;
			*)
				echo "Unknown build type!"
				exit 3
				;;
		esac
	fi
	
	mkdir ./$OutputDir
	CurrentBuildDir=${OutputDir}/${BuildDir}/${BuildType}
	cmake CMakeLists.txt -B${CurrentBuildDir} -DCMAKE_BUILD_TYPE=${BuildType} -DBUILD_OUTPUT_BIN=${OutputDir}/Bin/${BuildType}
	
elif [ $1 = "clean" ]; then
	rm -rf ./${BuildDir}
	rm -rf ./${OutputDir}
elif [ $1 = "help" ]; then
	PrintHelp $2
else
	echo "Unknown command!"
fi

exit 0
