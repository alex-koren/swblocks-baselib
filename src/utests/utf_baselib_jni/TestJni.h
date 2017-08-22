/*
 * This file is part of the swblocks-baselib library.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <baselib/jni/JavaVirtualMachine.h>
#include <baselib/jni/JniEnvironment.h>
#include <baselib/jni/JniResourceWrappers.h>
#include <baselib/jni/DirectByteBuffer.h>
#include <baselib/jni/JavaBridge.h>

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfArgsParser.h>

namespace
{
    using namespace bl;
    using namespace bl::jni;

    std::string getTestJar()
    {
        auto testJarFilePath =
            fs::normalize( test::UtfArgsParser::argv0() )
            .parent_path() / "jars/java-bridge.jar";

        testJarFilePath = fs::normalize( testJarFilePath );

        BL_CHK_T(
            false,
            fs::exists( testJarFilePath ),
            JavaException(),
            BL_MSG()
                << "Failed to locate jar file "
                << fs::normalizePathParameterForPrint( testJarFilePath )
            );

        return testJarFilePath.string();
    }

    class JniTestGlobalFixture
    {
    public:

        JniTestGlobalFixture()
        {
            JavaVirtualMachineConfig jvmConfig;

            jvmConfig.setClassPath( getTestJar() );
            jvmConfig.setThreadStackSize( "128M" );
            jvmConfig.setInitialHeapSize( "64M" );
            jvmConfig.setMaximumHeapSize( "512M" );

            jvmConfig.setCheckJni( true );
            jvmConfig.setVerboseJni( false );
            jvmConfig.setPrintGCDetails( false );
            jvmConfig.setTraceClassLoading( false );
            jvmConfig.setTraceClassUnloading( false );

            JavaVirtualMachine::setConfig( std::move( jvmConfig ) );

            ( void ) JavaVirtualMachine::instance();
        }

        ~JniTestGlobalFixture() NOEXCEPT
        {
            BL_NOEXCEPT_BEGIN()

            JavaVirtualMachine::destroy();

            BL_NOEXCEPT_END()
        }
    };

    template
    <
        typename T
    >
    void fastRequireEqual( const T& t1, const T& t2 )
    {
        if( t1 == t2 )
        {
            return;
        }

        UTF_REQUIRE_EQUAL( t1, t2 );
    }

} // __unnamed

UTF_GLOBAL_FIXTURE( JniTestGlobalFixture )

UTF_AUTO_TEST_CASE( Jni_CreateJniEnvironments )
{
    using namespace bl;
    using namespace bl::jni;

    const auto createJniEnvironment = []( SAA_in const bool detachJniEnvAfterTest )
    {
        const auto& environment = JniEnvironment::instance();
        UTF_REQUIRE_EQUAL( environment.getVersion(), JNI_VERSION_1_8 );

        if( detachJniEnvAfterTest )
        {
            JniEnvironment::detach();
        }
    };

    createJniEnvironment( false /* detachJniEnvAfterTest */ );

    const int numThreads = 10;

    os::thread threads[ numThreads ];

    for( int i = 0; i < numThreads; ++i )
    {
        threads[i] = os::thread( createJniEnvironment, i % 2 == 0 /* detachJniEnvAfterTest */ );
    }

    for( int i = 0; i < numThreads; ++i )
    {
        threads[i].join();
    }
}

UTF_AUTO_TEST_CASE( Jni_LocalGlobalReferences )
{
    using namespace bl;
    using namespace bl::jni;

    const auto& environment = JniEnvironment::instance();

    const auto localReference = environment.findJavaClass( "java/lang/String" );
    UTF_REQUIRE( localReference.get() != nullptr );

    const auto globalReference = environment.createGlobalReference< jclass >( localReference );
    UTF_REQUIRE( globalReference.get() != nullptr );

    {
        /*
         * Verify local and global references in main and non main threads.
         */

        const auto verifyReferences = [ &localReference, &globalReference ]( SAA_in const bool isMainThread )
        {
            const auto& environment = JniEnvironment::instance();
            JNIEnv* jniEnv = environment.getRawPtr();

            if( isMainThread )
            {
                UTF_REQUIRE_EQUAL( jniEnv -> GetObjectRefType( localReference.get() ), JNILocalRefType );
            }

            UTF_REQUIRE_EQUAL( jniEnv -> GetObjectRefType( globalReference.get() ), JNIGlobalRefType );
        };

        verifyReferences( true /* isMainThread */ );

        os::thread thread( verifyReferences, false /* isMainThread */ );
        thread.join();
    }
}

UTF_AUTO_TEST_CASE( Jni_JavaExceptions )
{
    using namespace bl;
    using namespace bl::jni;

    const auto& environment = JniEnvironment::instance();

    UTF_CHECK_THROW_MESSAGE(
        environment.findJavaClass( "no/such/class" ),
        JavaException,
        R"(Java class 'no/such/class' not found
Exception in thread "Thread-1" java.lang.NoClassDefFoundError: no/such/class
Caused by: java.lang.ClassNotFoundException: no.such.class
    at java.net.URLClassLoader.findClass(URLClassLoader.java:381)
    at java.lang.ClassLoader.loadClass(ClassLoader.java:424)
    at sun.misc.Launcher$AppClassLoader.loadClass(Launcher.java:331)
    at java.lang.ClassLoader.loadClass(ClassLoader.java:357))"
        );

    const auto threadClass = environment.findJavaClass( "java/lang/Thread" );

    const auto threadGetName = environment.getMethodID( threadClass.get(), "getName", "()Ljava/lang/String;" );
    UTF_REQUIRE( threadGetName != nullptr );

    UTF_CHECK_THROW_MESSAGE(
        environment.getMethodID( threadClass.get(), "foo", "()Ljava/lang/String;" ),
        JavaException,
        "Method 'foo' with signature '()Ljava/lang/String;' not found in class 'java.lang.Thread'"
        );

    const auto threadCurrentThread = environment.getStaticMethodID( threadClass.get(), "currentThread", "()Ljava/lang/Thread;" );
    UTF_REQUIRE( threadCurrentThread != nullptr );

    UTF_CHECK_THROW_MESSAGE(
        environment.getStaticMethodID( threadClass.get(), "foo", "()Ljava/lang/Thread;" ),
        JavaException,
        "Static method 'foo' with signature '()Ljava/lang/Thread;' not found in class 'java.lang.Thread'"
        );
}

UTF_AUTO_TEST_CASE( Jni_JavaBridge )
{
    using namespace bl;
    using namespace bl::jni;

    enum TestCase : std::int32_t
    {
        PerfTest = 0,
        ObjectInstanceTest = 1
    };

    const std::string javaBridgeClassName           = "com/jpmc/swblocks/baselib/test/JavaBridge";
    const std::string javaBridgeSingletonClassName  = "com/jpmc/swblocks/baselib/test/JavaBridgeSingleton";

    /*
     * Object instance test
     */

    const auto objectInstanceTest = [](
        SAA_in  const std::string&          javaClassName,
        SAA_in  const int32_t               expectedIndex
        )
    {
        const JavaBridge javaBridge( javaClassName );

        const std::size_t bufferSize = 128U;

        const DirectByteBuffer inDirectByteBuffer( bufferSize );
        const DirectByteBuffer outDirectByteBuffer( bufferSize );

        inDirectByteBuffer.prepareForWrite();
        inDirectByteBuffer.getBuffer() -> write( TestCase::ObjectInstanceTest );

        javaBridge.dispatch( inDirectByteBuffer, outDirectByteBuffer );

        const auto& outBuffer = outDirectByteBuffer.getBuffer();

        std::string outClassName;
        outBuffer -> read( &outClassName );
        UTF_REQUIRE_EQUAL( str::replace_all_copy( javaClassName, "/", "." ), outClassName );

        std::int32_t objectIndex;
        outBuffer -> read( &objectIndex );
        UTF_REQUIRE_EQUAL( objectIndex, expectedIndex );

        UTF_REQUIRE_EQUAL( outBuffer -> offset1(), outBuffer -> size() );
    };

    for( std::int32_t index = 0; index < 10; ++index )
    {
        objectInstanceTest( javaBridgeClassName, index );
        objectInstanceTest( javaBridgeSingletonClassName, 0 /* expectedIndex */ );
    }

    /*
     * Performance test
     */

    const auto perfTest = [](
        SAA_in  const std::string&          javaClassName,
        SAA_in  const int                   count
        )
    {
        const JavaBridge javaBridge( javaClassName );

        const std::size_t bufferSize = 64U;

        const DirectByteBuffer inDirectByteBuffer( bufferSize );
        const DirectByteBuffer outDirectByteBuffer( bufferSize );

        for( int i = 0; i < count; ++i )
        {
            /*
             * Write into input buffer
             */

            inDirectByteBuffer.prepareForWrite();

            const auto& inBuffer = inDirectByteBuffer.getBuffer();

            inBuffer -> write( TestCase::PerfTest );

            inBuffer -> write( std::int8_t( 123 ) );
            inBuffer -> write( std::int16_t( 12345 ) );
            inBuffer -> write( std::int32_t( 123456 ) );
            inBuffer -> write( std::int64_t( 12345678L ) );

            std::string inString = "the string " + std::to_string( i );
            inBuffer -> write( inString );

            /*
             * Call JavaBridge and read from output buffer
             */

            javaBridge.dispatch( inDirectByteBuffer, outDirectByteBuffer );

            const auto& outBuffer = outDirectByteBuffer.getBuffer();
            fastRequireEqual( inBuffer -> size() - sizeof( TestCase ),  outBuffer -> size() );

            std::int8_t int8;
            outBuffer -> read( &int8 );
            fastRequireEqual( int8, static_cast< std::int8_t >( 123 ) );

            std::int16_t int16;
            outBuffer -> read( &int16 );
            fastRequireEqual( int16, static_cast< std::int16_t >( 12345 ) );

            std::int32_t int32;
            outBuffer -> read( &int32 );
            fastRequireEqual( int32, static_cast< std::int32_t >( 123456 ) );

            std::int64_t int64;
            outBuffer -> read( &int64 );
            fastRequireEqual( int64, static_cast< std::int64_t >( 12345678L ) );

            std::string outString;
            outBuffer -> read( &outString );
            fastRequireEqual( outString, str::to_upper_copy( inString ) );

            fastRequireEqual( outBuffer -> offset1(), outBuffer -> size() );
        }
    };

    const auto runPerfTest = [ &perfTest ]( SAA_in const std::string& javaClassName )
    {
        const auto now = time::microsec_clock::universal_time();

        perfTest( javaClassName, 50000 /* count */ );

        const auto elapsed = time::microsec_clock::universal_time() - now;

        BL_LOG(
            Logging::debug(),
            BL_MSG()
                << "JavaBridge dispatch test time: "
                << elapsed
                << " ["
                << javaClassName
                << "]"
            );

         UTF_REQUIRE( elapsed < time::milliseconds( 5000 ) );
    };

    runPerfTest( javaBridgeClassName );
    runPerfTest( javaBridgeSingletonClassName );
}

UTF_AUTO_TEST_CASE( Jni_JVMOptions )
{
    const std::string json = R"(
        {
            "JVMOptions" : {
                "classPath" : "/path/to/jar",
                "threadStackSize" : "128M",
                "initialHeapSize" : "64M",
                "maximumHeapSize" : "512M",
        
                "checkJni" : true,
                "verboseJni" : true,
                "printGCDetails" : true,
                "traceClassLoading" : true,
                "traceClassUnloading" : true
            }
        }
        )";

    JavaVirtualMachineConfig jvmConfig( json );

    UTF_REQUIRE_EQUAL( jvmConfig.getClassPath(), "/path/to/jar" );
    UTF_REQUIRE_EQUAL( jvmConfig.getThreadStackSize(), "128M" );
    UTF_REQUIRE_EQUAL( jvmConfig.getInitialHeapSize(), "64M" );
    UTF_REQUIRE_EQUAL( jvmConfig.getMaximumHeapSize(), "512M" );

    UTF_REQUIRE_EQUAL( jvmConfig.getCheckJni(), true );
    UTF_REQUIRE_EQUAL( jvmConfig.getVerboseJni(), true );
    UTF_REQUIRE_EQUAL( jvmConfig.getPrintGCDetails(), true );
    UTF_REQUIRE_EQUAL( jvmConfig.getTraceClassLoading(), true );
    UTF_REQUIRE_EQUAL( jvmConfig.getTraceClassUnloading(), true );

    /*
     * Invalid JVM option
     */

    const std::string jsonInvalidOption = R"(
        {
            "JVMOptions" : {
                "invalidOption" : "value"
            }
        }
        )";

    UTF_CHECK_THROW_MESSAGE(
        JavaVirtualMachineConfig jvmConfig( jsonInvalidOption ),
        JsonException,
        "Unsupported JVM option 'invalidOption'"
        );
}
