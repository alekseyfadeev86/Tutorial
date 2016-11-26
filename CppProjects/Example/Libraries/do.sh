#!/bin/sh
CurDir=${PWD}
ResDir=Out
OutputDir=${CurDir}/"Output"

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

IsShared=0
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

		if [ $# -gt 2 ] && [ $3 = "shared" ]; then
			IsShared=1
		fi
	fi
	
	mkdir $OutputDir
	CurrentBuildDir=${OutputDir}/${BuildType}

	if [ $IsShared -eq 1 ]; then
		cmake CMakeLists.txt -B${CurrentBuildDir} -DROOT=${CurDir} -DRES_DIR=${ResDir} -DSHARED_LIB=${IsShared} -DCMAKE_BUILD_TYPE=${BuildType}
	else
		cmake CMakeLists.txt -B${CurrentBuildDir} -DROOT=${CurDir} -DRES_DIR=${ResDir} -DCMAKE_BUILD_TYPE=${BuildType}
	fi
	
elif [ $1 = "clean" ]; then
	rm -rf ${OutputDir}
	rm -rf ${CurDir}/${ResDir}
elif [ $1 = "help" ]; then
	PrintHelp $2
else
	echo "Unknown command!"
fi

exit 0
