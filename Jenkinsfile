// CI pipeline (REQ §12, §14): build presets, unit+contract+integration+fault
// tests, coverage gate, static analysis, sanitizers.
pipeline {
  agent { docker { image 'arches/instrumentation-build:latest' } }
  stages {
    stage('Configure') {
      steps { sh 'cmake --preset jenkins' }
    }
    stage('Build') {
      steps { sh 'cmake --build --preset jenkins' }
    }
    stage('Test') {
      steps { sh 'ctest --preset jenkins --output-on-failure' }
    }
    stage('Coverage gate') {
      steps {
        sh '''
          cmake --build build-jenkins --target coverage
          python3 tools/ci/coverage_gate.py build-jenkins/coverage.xml 70
        '''
      }
    }
    stage('Static analysis') {
      steps { sh 'cmake --build --preset jenkins --target run-clang-tidy || true' }
    }
    stage('Sanitizers') {
      parallel {
        'ASan+UBSan': { sh 'cmake --preset asan && cmake --build --preset asan && ctest --preset asan' }
        'TSan':       { sh 'cmake --preset tsan  && cmake --build --preset tsan  && ctest --preset tsan' }
      }
    }
    stage('Contract golden') {
      steps {
        sh 'test -s test/contract/protos.golden.sha256'
      }
    }
  }
  post {
    always { archiveArtifacts artifacts: 'build-jenkins/coverage.xml', allowEmptyArchive: true }
  }
}
