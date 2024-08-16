
pipeline {

  agent none

  stages {

    stage ('Build sourcetar') {
      steps {
        script {

          def sourceJob = build job: 'aws-galera-4-ee-sourcetar',  wait: true,
            parameters: [
              string( name: 'GIT_TARGET', value: env.GIT_TARGET ),
              booleanParam( name: 'HOTFIX_BUILD', value: env.HOTFIX_BUILD)
              ]
          env.SRCTAR_JOB = sourceJob.getNumber().toString()
        }
      }
    }

    stage ('Build binary packages') {

    parallel {
      stage ('Build bintar') {
        steps {
          script {
            def bintarJob = build job: 'aws-galera-4-ee-bintar', wait: true,
              parameters: [string(name: 'BUILD_SELECTOR', value: env.SRCTAR_JOB )]
            env.BINTAR_JOB = bintarJob.getNumber().toString()
            }
          }
        }
      stage ('Build rpm packages') {
        steps {
          script {
            def rpmJob = build job: 'aws-galera-4-ee-rpm-packages', wait: true,
              parameters: [string(name: 'BUILD_SELECTOR', value: env.SRCTAR_JOB )]
            env.RPM_JOB = rpmJob.getNumber().toString()
            }
          }
        }
      stage ('Build deb packages') {
        steps {
          script {
            def debJob = build job: 'aws-galera-4-ee-deb-packages', wait: true,
              parameters: [string(name: 'BUILD_SELECTOR', value: env.SRCTAR_JOB )]
            env.DEB_JOB = debJob.getNumber().toString()
            }
          }
        }
      } // parallel

    } // Build binary packages

    stage ('Run tests') {
      parallel {
        stage('Run bintar test') {
          steps {
            build job: 'run-galera-4-ee-release-test', wait: true,
              parameters: [string(name: 'BUILD_SELECTOR', value: env.BINTAR_JOB )]
          }
        }
        stage ('Run RPM test') {
          steps {
            build job: 'run-galera-4-ee-rpm-test', wait: true,
              parameters: [string(name: 'BUILD_SELECTOR', value: env.RPM_JOB )]
          }
        }
        stage ('Run DEB test') {
          steps {
            build job: 'run-galera-4-ee-deb-test', wait: true,
              parameters: [string(name: 'BUILD_SELECTOR', value: env.DEB_JOB )]
          }
        }
        stage ('Run SST RPM test') {
          steps {
            build job: 'run-galera-4-ee-systemd-sst-rpm-test', wait: true,
              parameters: [string(name: 'BUILD_SELECTOR', value: env.RPM_JOB )]
          }
        }
        stage ('Run SST DEB test') {
          steps {
            build job: 'run-galera-4-ee-systemd-sst-deb-test', wait: true,
              parameters: [string(name: 'BUILD_SELECTOR', value: env.DEB_JOB )]
          }
        }
      } // parallel
    }

  } // stages

}
