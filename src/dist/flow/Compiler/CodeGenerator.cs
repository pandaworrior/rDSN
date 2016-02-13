﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Linq.Expressions;
using System.Reflection;
using System.Diagnostics;
using System.IO;

using rDSN.Tron.Utility;
using rDSN.Tron.Contract;

namespace rDSN.Tron.Compiler
{
    //
    // build compilable query
    //  all external values must be converted into constant
    //  all external functions and types must be referenced with full namespace
    //    
    //
    public class CodeGenerator
    {
        private CodeBuilder _builder = new CodeBuilder();
        private QueryContext[] _contexts = null;
        private UInt64 _appId = RandomGenerator.Random64();
        private string _appClassName;
        private Dictionary<Type, string> _rewrittenTypes = new Dictionary<Type, string>();

        public UInt64 AppId { get { return _appId; } }
        
        public string Build(string className, QueryContext[] contexts)
        {
            //_stages = stages;
            _contexts = contexts;
            _appClassName = className;

            //BuildInputOutputValueTypes();
            BuildRewrittenTypes();                       

            BuildHeader();

            _builder.AppendLine("public class " + _appClassName + " : ServiceMesh");
            _builder.BeginBlock();

            BuildConstructor();
            BuildServiceClients();
            BuildServiceCalls();
            foreach (var c in contexts)
                BuildQuery(c);
            
            _builder.EndBlock();
            
            BuildFooter();
            return _builder.ToString();
        }
                
        private void BuildConstructor()
        {
            _builder.AppendLine("public " + _appClassName + "()");
            _builder.BeginBlock();

            _builder.EndBlock();
            _builder.AppendLine();
        }

        //private void BuildInputOutputValueTypes()
        //{
        //    if (_primaryContext.OutputType.IsSymbols())
        //    {
        //        throw new Exception("we are not support ISymbolCollection<> output right now, you can use an Gather method to merge it into a single ISymbol<>");
        //    }

        //    Trace.Assert(_primaryContext.InputType.IsSymbol() && _primaryContext.InputType.IsGenericType);

        //    Trace.Assert(_primaryContext.OutputType.IsSymbol() && _primaryContext.OutputType.IsGenericType);

        //    _inputValueType = _primaryContext.InputType.GetGenericArguments()[0];
        //    _outputValueType = _primaryContext.OutputType.GetGenericArguments()[0];

        //}

        private void BuildServiceClients()
        {
            foreach (var s in _contexts.SelectMany(c => c.Services).DistinctBy(s => s.Key.Member.Name))
            {
                _builder.AppendLine("private " + s.Value.Schema.FullName.GetCompilableTypeName() + "_Proxy " + s.Key.Member.Name + " = null;");
                _builder.AppendLine();
            }

            _builder.AppendLine("protected override ErrorCode InitServicesAndClients()");
            _builder.AppendLine("{");
            _builder++;

            _builder.AppendLine("IBondTransportClient client;");
            _builder.AppendLine("ErrorCode err;");
            _builder.AppendLine();

            foreach (var s in _contexts.SelectMany(c => c.Services).DistinctBy(s => s.Key.Member.Name))
            {
                _builder.AppendLine("err = InitService(\"" + s.Value.PackageName + "\", \"" + s.Value.URL + "\", \"" + s.Value.Name + "\");");
                _builder.AppendLine("if (err != ErrorCode.Success && err != ErrorCode.AppServiceAlreadyExist) return err; ");
                _builder.AppendLine("err = InitClient(\"" + s.Value.Name + "\", out client);");
                _builder.AppendLine("if (err != ErrorCode.Success) return err; ");
                _builder.AppendLine(s.Key.Member.Name + " = new " + s.Value.Schema.FullName + "_Proxy(client);");
                _builder.AppendLine();
            }

            _builder.AppendLine();
            _builder.AppendLine("return 0;");

            _builder--;
            _builder.AppendLine("}");
            _builder.AppendLine();
        }

        private void BuildServiceCalls()
        {
            HashSet<string> calls = new HashSet<string>();
            foreach (var s in _contexts.SelectMany(c => c.ServiceCalls))
            {
                Trace.Assert(s.Key.Object != null && s.Key.Object.NodeType == ExpressionType.MemberAccess);
                string svcName = (s.Key.Object as MemberExpression).Member.Name;
                string svcTypeName = s.Key.Object.Type.GetCompilableTypeName(_rewrittenTypes);
                string callName = s.Key.Method.Name;
                string respTypeName = s.Key.Type.GetCompilableTypeName(_rewrittenTypes);
                string reqTypeName = s.Key.Arguments[0].Type.GetCompilableTypeName(_rewrittenTypes);
                string call = "Call_" + s.Value.Schema.Name + "_" + callName;

                if (!calls.Add(call + ":" + reqTypeName))
                    continue;

                _builder.AppendLine("private " + respTypeName + " " + call  + "( " + reqTypeName + " req)");
                _builder.AppendLine("{");
                _builder++;
                _builder.AppendLine("while (true)");
                _builder.AppendLine("{");
                _builder++;

                // try block
                _builder.AppendLine("try");
                _builder.AppendLine("{");
                _builder++;

                _builder.AppendLine(" return " + svcName + "."  + callName  + "(req);");

                _builder--;
                _builder.AppendLine("}");


                // catch block
                _builder.AppendLine("catch(Exception e)");
                _builder.AppendLine("{");
                _builder++;
                _builder.AppendLine("Console.WriteLine(\"Exception during call " + svcName  + "." + callName + ", msg = \" + e.Message + \"\");");
                _builder.AppendLine("IBondTransportClient client;");
                _builder.AppendLine("while (ErrorCode.Success != InitClient(\"" + s.Value.Name + "\", out client)) Thread.Sleep(500);");
                _builder.AppendLine(svcName + " = new " + s.Value.Schema.FullName.GetCompilableTypeName() + "_Proxy(client);");
                _builder--;                
                _builder.AppendLine("}");

                _builder--;
                _builder.AppendLine("}");
                _builder--;
                _builder.AppendLine("}");
                _builder.AppendLine();
            }
        }
        
        private void BuildQuery(QueryContext c)
        {
            _builder.AppendLine("public " + c.OutputType.GetCompilableTypeName(_rewrittenTypes)
                        + " " + c.Name + "(" + c.InputType.GetCompilableTypeName(_rewrittenTypes) + " request)");
            
            _builder.AppendLine("{");
            _builder++;

            _builder.AppendLine("Console.Write(\".\");");

            // local vars
            foreach (var s in c.TempSymbolsByAlias)
            {
                _builder.AppendLine(s.Value.Type.GetCompilableTypeName(_rewrittenTypes) + " " + s.Key + ";");
            }

            if (c.TempSymbolsByAlias.Count > 0)
                _builder.AppendLine();

            // final query
            ExpressionToCode codeBuilder = new ExpressionToCode(c.RootExpression, c);
            string code = codeBuilder.GenCode(_builder.Indent);

            _builder.AppendLine(code + ";");

            _builder--;
            _builder.AppendLine("}");
            _builder.AppendLine();
        }
        
        private string VerboseStringArray(string[] parameters)
        {
            string ps = "";
            foreach (var s in parameters)
            {
                ps += "@\"" + s + "\",";
            }
            if (ps.Length > 0)
            {
                ps = ps.Substring(0, ps.Length - 1);
            }
            return ps;
        }
        
        private void BuildRewrittenTypes()
        {
            foreach (var c in _contexts)
            {
                foreach (var t in c.RewrittenTypes)
                {
                    if (!_rewrittenTypes.ContainsKey(t.Key))
                    {
                        _rewrittenTypes.Add(t.Key, t.Value);
                    }
                }
            }

            foreach (var c in _contexts)
            {
                c.RewrittenTypes = _rewrittenTypes;
            }

            foreach (var typeMap in _rewrittenTypes)
            {
                _builder.AppendLine("class " + typeMap.Value);
                _builder.AppendLine("{");
                _builder++;

                foreach (var property in typeMap.Key.GetProperties())
                {
                    _builder.AppendLine("public " + property.PropertyType.GetCompilableTypeName(_rewrittenTypes) + " " + property.Name + " { get; set; }");
                }

                _builder.AppendLine("public " + typeMap.Value + " () {}");

                _builder--;
                _builder.AppendLine("}");
                _builder.AppendLine();
            }
        }

        private void BuildHeader()
        {
            _builder.AppendLine("/* AUTO GENERATED BY Tron AT " + DateTime.Now.ToLocalTime().ToString() + " */");


            HashSet<string> namespaces = new HashSet<string>();
            namespaces.Add("System");
            namespaces.Add("System.IO");
            namespaces.Add("System.Collections.Generic");
            namespaces.Add("System.Linq");
            namespaces.Add("System.Text");
            namespaces.Add("System.Linq.Expressions");
            namespaces.Add("System.Reflection");
            namespaces.Add("System.Diagnostics");
            namespaces.Add("System.Net");
            namespaces.Add("System.Threading");

            namespaces.Add("rDSN.Tron.Utility");
            //namespaces.Add("rDSN.Tron.Compiler");
            namespaces.Add("rDSN.Tron.Contract");
            namespaces.Add("rDSN.Tron.Runtime");

            namespaces.Add("BondNetlibTransport");
            namespaces.Add("BondTransport");

            foreach (var nm in _contexts.SelectMany(c => c.Methods).Select(mi => mi.DeclaringType.Namespace).Distinct().Except(namespaces))
            {
                namespaces.Add(nm);
            }
            
            foreach (var np in namespaces)
            {
                _builder.AppendLine("using " + np + ";");
            }

            _builder.AppendLine();

            _builder.AppendLine("namespace rDSN.Tron.App");
            _builder.AppendLine("{");
            _builder++;
        }

        private void BuildFooter()
        {
            _builder--;
            _builder.AppendLine("} // end namespace");
            _builder.AppendLine();
        }

    }
}