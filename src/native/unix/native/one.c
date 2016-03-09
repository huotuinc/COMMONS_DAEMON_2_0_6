
#include "one.h"
#include "jsvc.h"
#include <stdio.h>

int addArg(int argc, char *argv[],char* data){
  argv[argc]=data;
  return argc+1;
}

int main(int myargc, char *myargv[]){
  int i=0;
  // printf("Before \n");
  // for(i=0;i<myargc;i++){
  //   printf("%s ",myargv[i]);
  // }
  // printf("\n");

  char *argv[100];
  int argc = 0;
  for (i = 0; i < myargc; i++) {
    argc=addArg(argc,argv,myargv[i]);
  }

  // -pidfile
  char CATALINA_PID[100];
  sprintf(CATALINA_PID,"%s/logs/catalina-daemon.pid",CATALINA_BASE);

  //CATALINA_OUT="$CATALINA_BASE/logs/catalina-daemon.out"
  char CATALINA_OUT[100];
  sprintf(CATALINA_OUT,"%s/logs/catalina-daemon.out",CATALINA_BASE);

  //CATALINA_TMP="$CATALINA_BASE/temp"
  char CATALINA_TMP[100];
  sprintf(CATALINA_TMP,"%s/temp",CATALINA_BASE);

  char CLASSPATH[1000];
  // # tomcat-juli.jar can be over-ridden per instance
  sprintf(CLASSPATH,".:%s/bin/bootstrap.jar:%s/bin/commons-daemon.jar:%s/bin/tomcat-juli.jar",CATALINA_HOME,CATALINA_HOME,CATALINA_HOME);

  char LOGGING_CONFIG[100];
  sprintf(LOGGING_CONFIG,"-Djava.util.logging.config.file=%s/conf/logging.properties",CATALINA_BASE);
  char* LOGGING_MANAGER="-Djava.util.logging.manager=org.apache.juli.ClassLoaderLogManager";
  char* JAVA_OPTS = LOGGING_MANAGER;

  char* CATALINA_MAIN="org.apache.catalina.startup.Bootstrap";

  char DBase[100];
  char DHome[100];
  char DTemp[100];
  sprintf(DBase,"-Dcatalina.base=%s",CATALINA_BASE);
  sprintf(DHome,"-Dcatalina.home=%s",CATALINA_HOME);
  sprintf(DTemp,"-Djava.io.tmpdir=%s",CATALINA_TMP);

// 只做start和stop
  // argc = addArg(argc,argv,"-cwd");
  // argc = addArg(argc,argv,CATALINA_BASE);
  if (strcmp(cmd,"start")==0) {
    // argc = addArg(argc,argv,"-java-home");
    // argc = addArg(argc,argv,JAVA_HOME);
    // user
    argc = addArg(argc,argv,"-user");
    argc = addArg(argc,argv,TOMCAT_USER);
    // wait
    argc = addArg(argc,argv,"-wait");
    argc = addArg(argc,argv,"10");
    // outfile
    argc = addArg(argc,argv,"-outfile");
    argc = addArg(argc,argv,CATALINA_OUT);

    argc = addArg(argc,argv,"-errfile");
    argc = addArg(argc,argv,"&1");

    argc = addArg(argc,argv,LOGGING_CONFIG);
    argc = addArg(argc,argv,JAVA_OPTS);
  }else{
    argc = addArg(argc,argv,"-stop");
  }

  argc = addArg(argc,argv,"-procname");
  argc = addArg(argc,argv,argv[0]);

  argc = addArg(argc,argv,"-home");
  argc = addArg(argc,argv,JAVA_HOME);
  // CATALINA_PID="$CATALINA_BASE/logs/catalina-daemon.pid"
  argc = addArg(argc,argv,"-pidfile");
  argc = addArg(argc,argv,CATALINA_PID);
  // classpath
  argc = addArg(argc,argv,"-classpath");
  argc = addArg(argc,argv,CLASSPATH);

  argc = addArg(argc,argv,"-Djava.endorsed.dirs=");
  argc = addArg(argc,argv,DBase);
  argc = addArg(argc,argv,DHome);
  argc = addArg(argc,argv,DTemp);

  argc = addArg(argc,argv,CATALINA_MAIN);


  // printf("After \n");
  // for(i=0;i<argc;i++){
  //   printf("%s ",argv[i]);
  // }
  // printf("\n");
  return apacheMain(argc,argv);
}
