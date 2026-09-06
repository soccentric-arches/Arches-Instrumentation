// CI pipeline (REQ §12, §14): build presets, formatting check, unit +
// contract + integration + fault-injection + secret-leak tests, coverage
// gate, static analysis, sanitizers.
//
// Nothing here is allowed to pass by not running. A stage that cannot do its
// job fails the build rather than being skipped — a green pipeline must mean
// the checks ran, not that they were unavailable.
pipeline {
  agent { docker { image 'arches/instrumentation-build:latest' } }
  options {
    timestamps()
    timeout(time: 60, unit: 'MINUTES')
  }
  stages {
    stage('Configure') {
      steps { sh 'cmake --preset jenkins' }
    }
    stage('Format check') {
      // .clang-format is the enforced style: a violation is a finding to fix,
      // not something to silently auto-fix on the way through.
      steps { sh 'cmake --build --preset jenkins --target check-format' }
    }
    stage('Build') {
      steps { sh 'cmake --build --preset jenkins' }
    }
    stage('Test') {
      steps { sh 'ctest --preset jenkins --output-on-failure' }
    }
    stage('Coverage gate') {
      steps {
        // §12 makes coverage a hard requirement, so the gate runs against a
        // dedicated instrumented build and the threshold is enforced.
        sh '''
          set -eu
          cmake --preset coverage
          cmake --build --preset coverage
          ctest --preset coverage --output-on-failure
          cmake --build build-coverage --target coverage
          python3 tools/ci/coverage_gate.py build-coverage/coverage.xml "${COVERAGE_MIN:-80}"
        '''
      }
    }
    stage('Static analysis') {
      // No "|| true": if clang-tidy reports something, that is a finding to
      // deal with, not output to discard.
      steps { sh 'cmake --build --preset jenkins --target run-clang-tidy' }
    }
    stage('Sanitizers') {
      parallel {
        stage('ASan+UBSan') {
          steps { sh 'cmake --preset asan && cmake --build --preset asan && ctest --preset asan --output-on-failure' }
        }
        stage('TSan') {
          steps { sh 'cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan --output-on-failure' }
        }
      }
    }
    stage('Contract golden') {
      steps {
        // The descriptor hash is checked by ContractProtos in the Test stage;
        // this guards against the golden itself being emptied to make that
        // test pass trivially.
        sh 'test -s test/contract/protos.golden.sha256'
      }
    }
  }
  post {
    always {
      archiveArtifacts artifacts: 'build-coverage/coverage.xml', allowEmptyArchive: true
      junit testResults: 'build-jenkins/Testing/**/*.xml', allowEmptyResults: true
    }
  }
}
