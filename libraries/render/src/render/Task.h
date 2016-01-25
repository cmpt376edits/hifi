//
//  Task.h
//  render/src/render
//
//  Created by Zach Pomerantz on 1/6/2016.
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_render_Task_h
#define hifi_render_Task_h

#include <qscriptengine.h> // QObject

#include "Context.h"

#include "gpu/Batch.h"
#include <PerfStat.h>

// Prepare Qt for auto configurations
Q_DECLARE_METATYPE(std::shared_ptr<QObject>)

namespace render {

// A varying piece of data, to be used as Job/Task I/O
// TODO: Task IO
class Varying {
public:
    Varying() {}
    Varying(const Varying& var) : _concept(var._concept) {}
    template <class T> Varying(const T& data) : _concept(std::make_shared<Model<T>>(data)) {}

    template <class T> T& edit() { return std::static_pointer_cast<Model<T>>(_concept)->_data; }
    template <class T> const T& get() { return std::static_pointer_cast<const Model<T>>(_concept)->_data; }

protected:
    class Concept {
    public:
        virtual ~Concept() = default;
    };
    template <class T> class Model : public Concept {
    public:
        using Data = T;

        Model(const Data& data) : _data(data) {}
        virtual ~Model() = default;

        Data _data;
    };

    std::shared_ptr<Concept> _concept;
};

// A default Config is always on; to create an enableable Config, use the ctor JobConfig(bool enabled)
class JobConfig : public QObject {
    Q_OBJECT
public:
    JobConfig() : alwaysEnabled{ true }, enabled{ true } {}
    JobConfig(bool enabled) : alwaysEnabled{ false }, enabled{ enabled } {}

    bool isEnabled() { return alwaysEnabled || enabled; }

    bool alwaysEnabled{ true };
    bool enabled;
};

class Task;

class TaskConfig : public JobConfig {
    Q_OBJECT
public:
    TaskConfig() : JobConfig() {}
    TaskConfig(bool enabled) : JobConfig(enabled) {}

    void init(Task* task) { _task = task; }

    template <class T> typename T::Config* getConfig(std::string job = "") const {
        QString name = job.empty() ? QString() : QString(job.c_str()); // an empty string is not a null string
        return findChild<typename T::Config*>(name);
    }

    template <class T> void setJobEnabled(bool enable = true, std::string job = "") const {
        getConfig<T>(name)->enabled = enable;
        refresh(); // trigger a Job->configure
    }
    template <class T> bool isJobEnabled(bool enable = true, std::string job = "") const {
        return getConfig<T>(job)->isEnabled();
    }

public slots:
    void refresh();

private:
    Task* _task;
};

template <class T, class C> void jobConfigure(T& model, const C& configuration) {
    model.configure(configuration);
}
template<class T> void jobConfigure(T&, const JobConfig&) {
}
template<class T> void jobConfigure(T&, const TaskConfig&) {
}

// FIXME: In c++17, use default classes of nullptr_t to combine these
template <class T> void jobRun(T& model, const SceneContextPointer& sceneContext, const RenderContextPointer& renderContext) {
    model.run(sceneContext, renderContext);
}
template <class T, class I> void jobRunI(T& model, const SceneContextPointer& sceneContext, const RenderContextPointer& renderContext, const I& input) {
    model.run(sceneContext, renderContext, input);
}
template <class T, class O> void jobRunO(T& model, const SceneContextPointer& sceneContext, const RenderContextPointer& renderContext, O& output) {
    model.run(sceneContext, renderContext, output);
}
template <class T, class I, class O> void jobRunIO(T& model, const SceneContextPointer& sceneContext, const RenderContextPointer& renderContext, const I& input, O& output) {
    model.run(sceneContext, renderContext, input, output);
}

class Job {
public:
    using Config = JobConfig;
    using QConfigPointer = std::shared_ptr<QObject>;

    // The guts of a job
    class Concept {
    public:
        Concept(QConfigPointer config) : _config(config) {}
        virtual ~Concept() = default;

        bool isEnabled() const { return _isEnabled; }
        void setEnabled(bool isEnabled) { _isEnabled = isEnabled; }

        virtual const Varying getInput() const { return Varying(); }
        virtual const Varying getOutput() const { return Varying(); }

        virtual QConfigPointer& getConfiguration() { return _config; }
        virtual void applyConfiguration() = 0;

        virtual void run(const SceneContextPointer& sceneContext, const RenderContextPointer& renderContext) = 0;

    protected:
        QConfigPointer _config;
        bool _isEnabled = true;
    };
    using ConceptPointer = std::shared_ptr<Concept>;

    template <class T, class C = Config> class Model : public Concept {
    public:
        using Data = T;

        Data _data;

        Model(Data data = Data()) : _data(data), Concept(std::make_shared<C>()) {
            applyConfiguration();
        }

        void applyConfiguration() {
            jobConfigure(_data, *std::static_pointer_cast<C>(_config));
        }

        void run(const SceneContextPointer& sceneContext, const RenderContextPointer& renderContext) {
            renderContext->jobConfig = std::static_pointer_cast<Job::Config>(_config);
            if (renderContext->jobConfig->alwaysEnabled || renderContext->jobConfig->enabled) {
                jobRun(_data, sceneContext, renderContext);
            }
            renderContext->jobConfig.reset();
        }
    };

    template <class T, class I, class C = Config> class ModelI : public Concept {
    public:
        using Data = T;
        using Input = I;

        Data _data;
        Varying _input;

        const Varying getInput() const { return _input; }

        ModelI(const Varying& input, Data data = Data()) : _input(input), _data(data), Concept(std::make_shared<C>()) {
            applyConfiguration();
        }

        void applyConfiguration() {
            jobConfigure(_data, *std::static_pointer_cast<C>(_config));
        }

        void run(const SceneContextPointer& sceneContext, const RenderContextPointer& renderContext) {
            renderContext->jobConfig = std::static_pointer_cast<Job::Config>(_config);
            if (renderContext->jobConfig->alwaysEnabled || renderContext->jobConfig->enabled) {
                jobRunI(_data, sceneContext, renderContext, _input.get<I>());
            }
            renderContext->jobConfig.reset();
        }
    };

    template <class T, class O, class C = Config> class ModelO : public Concept {
    public:
        using Data = T;
        using Output = O;

        Data _data;
        Varying _output;

        const Varying getOutput() const { return _output; }

        ModelO(Data data = Data()) : _data(data), _output(Output()), Concept(std::make_shared<C>()) {
            applyConfiguration();
        }

        void applyConfiguration() {
            jobConfigure(_data, *std::static_pointer_cast<C>(_config));
        }

        void run(const SceneContextPointer& sceneContext, const RenderContextPointer& renderContext) {
            renderContext->jobConfig = std::static_pointer_cast<Job::Config>(_config);
            if (renderContext->jobConfig->alwaysEnabled || renderContext->jobConfig->enabled) {
                jobRunO(_data, sceneContext, renderContext, _output.edit<O>());
            }
            renderContext->jobConfig.reset();
        }
    };

    template <class T, class I, class O, class C = Config> class ModelIO : public Concept {
    public:
        using Data = T;
        using Input = I;
        using Output = O;

        Data _data;
        Varying _input;
        Varying _output;

        const Varying getInput() const { return _input; }
        const Varying getOutput() const { return _output; }

        ModelIO(const Varying& input, Data data = Data()) : _data(data), _input(input), _output(Output()), Concept(std::make_shared<C>()) {
            applyConfiguration();
        }

        void applyConfiguration() {
            jobConfigure(_data, *std::static_pointer_cast<C>(_config));
        }

        void run(const SceneContextPointer& sceneContext, const RenderContextPointer& renderContext) {
            renderContext->jobConfig = std::static_pointer_cast<Job::Config>(_config);
            if (renderContext->jobConfig->alwaysEnabled || renderContext->jobConfig->enabled) {
                jobRunIO(_data, sceneContext, renderContext, _input.get<I>(), _output.edit<O>());
            }
            renderContext->jobConfig.reset();
        }
    };

    Job(std::string name, ConceptPointer concept) : _concept(concept), _name(name) {}

    bool isEnabled() const { return _concept->isEnabled(); }
    void setEnabled(bool isEnabled) { _concept->setEnabled(isEnabled); }

    const Varying getInput() const { return _concept->getInput(); }
    const Varying getOutput() const { return _concept->getOutput(); }
    QConfigPointer& getConfiguration() const { return _concept->getConfiguration(); }
    void applyConfiguration() { return _concept->applyConfiguration(); }

    template <class T> T& edit() {
        auto concept = std::static_pointer_cast<typename T::JobModel>(_concept);
        assert(concept);
        return concept->_data;
    }

    void run(const SceneContextPointer& sceneContext, const RenderContextPointer& renderContext) {
        PerformanceTimer perfTimer(_name.c_str());
        PROFILE_RANGE(_name.c_str());

        _concept->run(sceneContext, renderContext);
    }

    protected:
    ConceptPointer _concept;
    std::string _name = "";
};

// A task is a specialized job to run a collection of other jobs
// It is defined with JobModel = Task::Model<T>
//
// A task with a custom config *must* use the templated constructor
class Task {
public:
    using Config = TaskConfig;
    using QConfigPointer = Job::QConfigPointer;

    template <class T, class C = Config> class Model : public Job::Concept {
    public:
        using Data = T;

        // TODO: Make Data a shared_ptr, and give Config a weak_ptr
        Data _data;

        // The _config is unused; the model delegates to its data's _config
        Model(Data data = Data()) : _data(data), Concept(std::make_shared<C>()) {
            _config = _data._config;
            std::static_pointer_cast<Config>(_config)->init(&_data);

            applyConfiguration();
        }

        void applyConfiguration() {
            jobConfigure(_data, *std::static_pointer_cast<C>(_config));
        }

        void run(const SceneContextPointer& sceneContext, const RenderContextPointer& renderContext) {
            if (isEnabled()) {
                jobRun(_data, sceneContext, renderContext);
            }
        }
    };

    using Jobs = std::vector<Job>;

    // A task must use its Config for construction
    Task() : _config{ std::make_shared<Config>() } {}
    template <class C> Task(std::shared_ptr<C> config) : _config{ config } {}

    // Queue a new job to the container; returns the job's output
    template <class T, class... A> const Varying addJob(std::string name, A&&... args) {
        _jobs.emplace_back(name, std::make_shared<typename T::JobModel>(std::forward<A>(args)...));
        QConfigPointer config = _jobs.back().getConfiguration();
        config->setParent(_config.get());
        config->setObjectName(name.c_str());
        QObject::connect(config.get(), SIGNAL(dirty()), _config.get(), SLOT(refresh()));
        return _jobs.back().getOutput();
    }

    std::shared_ptr<Config> getConfiguration() {
        auto config = std::static_pointer_cast<Config>(_config);
        // If we are here, we were not made by a Model, so we must initialize our own config
        config->init(this);
        return config;
    }

    void configure(const QObject& configuration) {
        for (auto& job : _jobs) {
            job.applyConfiguration();
        }
    }
    void enableJob(size_t jobIndex, bool enable = true) { _jobs.at(jobIndex).setEnabled(enable); }
    bool getEnableJob(size_t jobIndex) const { return _jobs.at(jobIndex).isEnabled(); }

protected:
    template <class T, class C> friend class Model;

    QConfigPointer _config { nullptr };
    Jobs _jobs;
};

}

#endif // hifi_render_Task_h
